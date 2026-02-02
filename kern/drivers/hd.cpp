#include "hd.h"
#include "stdio.h"
#include "string.h"

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>
#include <arch/x86/drivers/i8259.h>
#include "pic.h"
#include "../sched/sched.h"
#include "../drivers/intr.h"

// Global IDE devices
IdeDevice IdeDevice::s_ide_devices[MAX_IDE_DEVICES] = {};
int IdeDevice::s_ide_devices_count = 0;

// Forward declarations
static int hd_wait_ready_on_base(uint16_t base);
static int hd_wait_data_on_base(uint16_t base);

// Device initialization configurations
static const struct {
    uint8_t channel;
    uint8_t drive;
    uint16_t base;
    uint16_t ctrl;
    uint8_t irq;
    const char *name;
} ide_configs[MAX_IDE_DEVICES] = {
    {0, 0, IDE0_BASE, IDE0_CTRL, IRQ_IDE1, "hda"},  // Primary Master
    {0, 1, IDE0_BASE, IDE0_CTRL, IRQ_IDE1, "hdb"},  // Primary Slave
    {1, 0, IDE1_BASE, IDE1_CTRL, IRQ_IDE2, "hdc"},  // Secondary Master
    {1, 1, IDE1_BASE, IDE1_CTRL, IRQ_IDE2, "hdd"},  // Secondary Slave
};

void IdeDevice::detect(int deviceID) {
    auto& config = ide_configs[deviceID];
    m_channel = config.channel;
    m_drive = config.drive;
    m_base = config.base;
    m_ctrl = config.ctrl;
    m_irq = config.irq;

    uint16_t identifyData[256]{};
    insw(m_base + IDE_DATA, identifyData, 256);

    m_type = BLK_TYPE_DISK;
    m_present = 1;

    // Parse disk info
    m_info.cylinders = identifyData[1];
    m_info.heads = identifyData[3];
    m_info.sectors = identifyData[6];
    m_info.size = static_cast<uint32_t>(identifyData[60]) | (static_cast<uint32_t>(identifyData[61]) << 16);
    m_info.valid = 1;

    strncpy(m_name, config.name, sizeof(m_name));
}

void IdeDevice::interupt() {
    do {
        uint8_t status = inb(m_base + IDE_STATUS);
        if (status & IDE_ERR) {
            uint8_t err = inb(m_base + IDE_ERROR);
            cprintf("hd_intr: disk error on %s (status=0x%02x, error=0x%02x)\n", m_name, status, err);
            m_err = -1;
            break;
        }

        // Read operation: read data when DRQ is set
        if (m_op == 1) {
            if (!(status & IDE_DRQ)) {
                break;
            }
            if (m_buffer) {
                insw(m_base + IDE_DATA, m_buffer, SECTOR_SIZE / 2);
            }
            break;
        }
        else if (m_op == 2) {
            if (!(status & IDE_DRQ)) {
                break;
            }
            if (m_buffer) {
                outsw(m_base + IDE_DATA, m_buffer, SECTOR_SIZE / 2);
            }
        }
    } while (0);

    m_irq_done = 1;
    if (m_waiting) {
        wakeup_proc(m_waiting);
    }
}

void IdeDevice::init(void) {
    // Enable IDE interrupts for both channels at PIC level
    pic::enable(IRQ_IDE1);
    pic::enable(IRQ_IDE2);

    // Try to detect all 4 possible devices
    for (int i = 0; i < MAX_IDE_DEVICES; i++) {
        auto& config = ide_configs[i];
        uint8_t driveSel = config.drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
        outb(config.base + IDE_DEVICE, 0xA0 | (driveSel << 4));
        io_wait();

        outb(config.base + IDE_COMMAND, IDE_CMD_IDENTIFY);
        io_wait();

        // Check if device is present
        uint8_t status = inb(config.base + IDE_STATUS);
        if (status == 0) {
            return; // No device
        }

        // Wait for BSY to clear
        if (hd_wait_ready_on_base(config.base) != 0) {
            return; // Device not ready
        }
        s_ide_devices[s_ide_devices_count++].detect(i);
    }
    
    cprintf("hd_init: found %d device(s)\n", s_ide_devices_count);
}

IdeDevice* IdeDevice::get_device(int deviceID) {
    if (deviceID < 0 || deviceID >= s_ide_devices_count) {
        return nullptr;
    }
    if (!s_ide_devices[deviceID].m_present) {
        return nullptr;
    }
    return &s_ide_devices[deviceID];
}

int IdeDevice::get_device_count() {
    return s_ide_devices_count;
}

int IdeDevice::read(uint32_t blockNumber, void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("IdeDevice::read: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("IdeDevice::read: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }
    
    uint8_t driveSel = m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Read blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(m_base) != 0) {
            cprintf("IdeDevice::read: device %s not ready\n", m_name);
            return -1;
        }

        // Use RAII for interrupt management during command setup and wait
        {
            InterruptsGuard guard;
            
            // Prepare transfer parameters
            m_irq_done = 0;
            m_err = 0;
            m_buffer = reinterpret_cast<uint8_t*>(buf) + i * SECTOR_SIZE;
            m_op = 1; // Read operation
            m_waiting = current;

            // Set sector count and LBA address
            outb(m_base + IDE_SECTOR_COUNT, 1);
            outb(m_base + IDE_LBA_LOW, lba & 0xFF);
            outb(m_base + IDE_LBA_MID, (lba >> 8) & 0xFF);
            outb(m_base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
            outb(m_base + IDE_DEVICE, driveSel | ((lba >> 24) & 0x0F));

            // Send read command (will trigger interrupt)
            outb(m_base + IDE_COMMAND, IDE_CMD_READ);
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_irq_done) {
            {
                InterruptsGuard guard;
                if (m_irq_done) break;  // Re-check with interrupts disabled
                current->state = TASK_SLEEPING;
            }
            schedule();
        }

        // Clean up state
        m_op = 0;
        m_waiting = nullptr;

        // Check for errors
        if (m_err) {
            cprintf("IdeDevice::read: error reading block %d from %s\n", lba, m_name);
            return -1;
        }
    }
    
    return 0;
}

int IdeDevice::write(uint32_t blockNumber, const void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("IdeDevice::write: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("IdeDevice::write: out of range (block %d + %d > %d)\n", blockNumber, blockCount, m_info.size);
        return -1;
    }
    
    uint8_t driveSel = m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(m_base) != 0) {
            cprintf("IdeDevice::write: device %s not ready\n", m_name);
            return -1;
        }

        // Use RAII for interrupt management during command setup
        {
            InterruptsGuard guard;
            
            // Prepare transfer parameters
            m_irq_done = 0;
            m_err = 0;
            m_buffer = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf) + i * SECTOR_SIZE);
            m_op = 2; // Write operation
            m_waiting = current;

            // Set sector count and LBA address
            outb(m_base + IDE_SECTOR_COUNT, 1);
            outb(m_base + IDE_LBA_LOW, lba & 0xFF);
            outb(m_base + IDE_LBA_MID, (lba >> 8) & 0xFF);
            outb(m_base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
            outb(m_base + IDE_DEVICE, driveSel | ((lba >> 24) & 0x0F));

            // Send write command
            outb(m_base + IDE_COMMAND, IDE_CMD_WRITE);

            // Wait for device ready to receive data (within critical section)
            if (hd_wait_ready_on_base(m_base) != 0) {
                m_op = 0;
                m_waiting = nullptr;
                return -1;  // guard destructor will restore interrupts
            }

            // Write the data
            outsw(m_base + IDE_DATA, m_buffer, SECTOR_SIZE / 2);
        }

        // Wait for interrupt completion (double-checked pattern)
        while (!m_irq_done) {
            {
                InterruptsGuard guard;
                if (m_irq_done) break;  // Re-check with interrupts disabled
                current->state = TASK_SLEEPING;
            }
            schedule();
        }
        
        // Clean up state
        m_op = 0;
        m_waiting = nullptr;

        // Check for errors
        if (m_err) {
            cprintf("IdeDevice::write: error writing block %d to %s\n", lba, m_name);
            return -1;
        }
    }
    
    return 0;
}

/**
 * Wait for disk to be ready (on specific base port)
 */
static int hd_wait_ready_on_base(uint16_t base) {
    int timeout = 100000;
    
    while (timeout-- > 0) {
        uint8_t status = inb(base + IDE_STATUS);

        if ((status & (IDE_BSY | IDE_DRDY)) == IDE_DRDY) {
            return 0;
        }
    }
    
    return -1;
}

/**
 * Wait for disk to be ready to transfer data (on specific base port)
 */
static int hd_wait_data_on_base(uint16_t base) {
    int timeout = 100000;
    
    while (timeout-- > 0) {
        uint8_t status = inb(base + IDE_STATUS);

        if ((status & (IDE_BSY | IDE_DRQ)) == IDE_DRQ) {
            return 0;
        }

        if (status & IDE_ERR) {
            return -1;
        }
    }
    
    return -1;
}

/**
 * Handle IDE interrupt (IRQ_IDE1 or IRQ_IDE2)
 * Called from trap handler to handle disk interrupts
 */
void IdeDevice::interrupt_handler(int irq) {
    int channel = (irq == IRQ_IDE2) ? 1 : 0;

    for (int i = 0; i < s_ide_devices_count; i++) {
        IdeDevice& dev = s_ide_devices[i];

        if (!dev.m_present || dev.m_channel != channel) {
            continue;
        }
        if (dev.m_op == 0) {
            continue;
        }

        dev.interupt();
    }
}

/**
 * Test disk read/write for all devices
 */
void IdeDevice::test(void) {
    cprintf("\n=== Multi-Disk Test ===\n");
    cprintf("Testing %d disk device(s)\n\n", IdeDevice::get_device_count());
    
    if (IdeDevice::get_device_count() == 0) {
        cprintf("No disk devices found!\n");
        cprintf("=== Test Complete ===\n\n");
        return;
    }
    
    // Allocate test buffers
    static uint8_t writeBuff[SECTOR_SIZE]{};
    static uint8_t readBuff[SECTOR_SIZE]{};
    // Test each device
    for (int i = 0; i < s_ide_devices_count; i++) {
        IdeDevice& dev = s_ide_devices[i];
        
        if (!dev.m_present) {
            continue;
        }
        
        cprintf("--- Testing %s (dev_id=%d) ---\n", dev.m_name, i);
        cprintf("  Size: %d sectors (%d MB)\n", dev.m_info.size, dev.m_info.size / 2048);
        
        // Fill write buffer with test pattern (unique per device)
        for (int i = 0; i < SECTOR_SIZE; i++) {
            writeBuff[i] = (uint8_t)((i + i * 17) & 0xFF);
        }
        
        // Use sector 100 for testing
        uint32_t testSector = 100;
        cprintf("  Test 1: Write sector %d...\n", testSector);
        if (dev.write(testSector, writeBuff, 1) != 0) {
            cprintf("    FAILED: write error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 2: Read sector %d...\n", testSector);
        if (dev.read(testSector, readBuff, 1) != 0) {
            cprintf("    FAILED: read error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 3: Verify data...\n");
        int errors = 0;
        for (int i = 0; i < SECTOR_SIZE; i++) {
            if (readBuff[i] != writeBuff[i]) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n", i, writeBuff[i], readBuff[i]);
                }
                errors++;
            }
        }
        
        if (errors > 0) {
            cprintf("    FAILED: %d mismatches\n", errors);
        } else {
            cprintf("    OK\n");
        }
        
        cprintf("  %s test %s\n\n", dev.m_name, errors == 0 ? "PASSED" : "FAILED");
    }
    
    cprintf("=== Multi-Disk Test Complete ===\n\n");
}

/**
 * Simple interrupt test - read one sector using polling first
 */
void IdeDevice::test_interrupt(void) {
    cprintf("\n=== IDE Interrupt Test ===\n");
    
    if (s_ide_devices_count == 0) {
        cprintf("No devices available for testing\n");
        return;
    }
    
    IdeDevice& dev = s_ide_devices[0];
    cprintf("Testing device: %s\n", dev.m_name);
    cprintf("  base=0x%x, ctrl=0x%x, irq=%d\n", dev.m_base, dev.m_ctrl, dev.m_irq);
    
    // Check interrupt enable status
    uint8_t ctrl = inb(dev.m_ctrl);
    cprintf("  Control register: 0x%02x (interrupts %s)\n", 
            ctrl, (ctrl & IDE_CTRL_nIEN) ? "DISABLED" : "ENABLED");
    
    // Check PIC mask
    cprintf("  Checking if IRQ %d is enabled in PIC...\n", dev.m_irq);
    
    // Try a simple read with interrupt
    cprintf("  Attempting interrupt-driven read of sector 0...\n");
    static uint8_t buf[SECTOR_SIZE]{};
    int result = dev.read(0, buf, 1);
    if (result == 0) {
        cprintf("  SUCCESS: Read completed\n");
    } else {
        cprintf("  FAILED: Read failed\n");
    }
    
    cprintf("=== Test Complete ===\n\n");
}