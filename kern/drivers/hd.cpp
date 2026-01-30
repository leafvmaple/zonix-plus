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
static IdeDevice ide_devices[MAX_IDE_DEVICES]{};
static int num_devices{};

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

    uint8_t driveSel = m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    outb(m_base + IDE_DEVICE, 0xA0 | (driveSel << 4));
    io_wait();

    outb(m_base + IDE_COMMAND, IDE_CMD_IDENTIFY);
    io_wait();

    // Check if device is present
    uint8_t status = inb(m_base + IDE_STATUS);
    if (status == 0) {
        return; // No device
    }

    // Wait for BSY to clear
    if (hd_wait_ready_on_base(m_base) != 0) {
        return; // Device not ready
    }

    // Read IDENTIFY data
    uint16_t identify_data[256];
    insw(m_base + IDE_DATA, identify_data, 256);

    // Parse disk info
    m_info.cylinders = identify_data[1];
    m_info.heads = identify_data[3];
    m_info.sectors = identify_data[6];
    m_info.size = static_cast<uint32_t>(identify_data[60]) | (static_cast<uint32_t>(identify_data[61]) << 16);
    m_info.valid = 1;
    m_present = 1;

    strncpy(m_name, ide_configs[deviceID].name, sizeof(m_name));
}

int IdeDevice::read(uint32_t blockNumber, void* buf, size_t blockCount) {
    if (!m_present) {
        cprintf("IdeDevice::read: device %s not present\n", m_name);
        return -1;
    }
    
    if (blockNumber + blockCount > m_info.size) {
        cprintf("IdeDevice::read: out of range (block %d + %d > %d)\n", 
                blockNumber, blockCount, m_info.size);
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

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
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

        while (!m_irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();

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
        cprintf("IdeDevice::write: out of range (block %d + %d > %d)\n", 
                blockNumber, blockCount, m_info.size);
        return -1;
    }
    
    uint8_t drive_sel = m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Write blocks one by one (interrupt-driven)
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t lba = blockNumber + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(m_base) != 0) {
            cprintf("IdeDevice::write: device %s not ready\n", m_name);
            return -1;
        }

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
        // Prepare transfer parameters
        m_irq_done = 0;
        m_err = 0;
        m_buffer = const_cast<void*>(static_cast<const void*>(
            reinterpret_cast<const uint8_t*>(buf) + i * SECTOR_SIZE));
        m_op = 2; // Write operation
        m_waiting = current;

        // Set sector count and LBA address
        outb(m_base + IDE_SECTOR_COUNT, 1);
        outb(m_base + IDE_LBA_LOW, lba & 0xFF);
        outb(m_base + IDE_LBA_MID, (lba >> 8) & 0xFF);
        outb(m_base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(m_base + IDE_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

        // Send write command
        outb(m_base + IDE_COMMAND, IDE_CMD_WRITE);

        // Wait for device ready to receive data
        if (hd_wait_ready_on_base(m_base) != 0) {
            intr_restore();
            return -1;
        }

        // Write the data
        outsw(m_base + IDE_DATA, m_buffer, SECTOR_SIZE / 2);

        while (!m_irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();
        
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
        
        // Check if busy bit is clear and ready bit is set
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
        
        // Check if busy bit is clear and data request bit is set
        if ((status & (IDE_BSY | IDE_DRQ)) == IDE_DRQ) {
            return 0;
        }
        
        // Check for error
        if (status & IDE_ERR) {
            return -1;
        }
    }
    
    return -1;
}

/**
 * Detect a single IDE device
 */
static int hd_detect_device(int dev_id) {
    IdeDevice *dev = &ide_devices[dev_id];
    uint16_t base = ide_configs[dev_id].base;
    uint16_t ctrl = ide_configs[dev_id].ctrl;
    uint8_t drive_sel = ide_configs[dev_id].drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Disable interrupts during device detection
    outb(ctrl, IDE_CTRL_nIEN);
    
    // Select device
    outb(base + IDE_DEVICE, drive_sel);
    
    // Wait 400ns for device selection (read status 4 times)
    for (int i = 0; i < 4; i++) {
        inb(base + IDE_STATUS);
    }
    
    // Send IDENTIFY command
    outb(base + IDE_COMMAND, IDE_CMD_IDENTIFY);
    
    // Check if device exists (status != 0 and != 0xFF)
    uint8_t status = inb(base + IDE_STATUS);
    if (status == 0 || status == 0xFF) {
        return -1;  // No device
    }
    
    // Wait for data
    if (hd_wait_data_on_base(base) != 0) {
        return -1;
    }
    
    // Read identification data
    uint16_t buf[256];
    insw(base + IDE_DATA, buf, 256);
    
    // Fill device structure
    dev->m_channel = ide_configs[dev_id].channel;
    dev->m_drive = ide_configs[dev_id].drive;
    dev->m_base = base;
    dev->m_ctrl = ide_configs[dev_id].ctrl;
    dev->m_irq = ide_configs[dev_id].irq;
    dev->m_info.cylinders = buf[1];
    dev->m_info.heads = buf[3];
    dev->m_info.sectors = buf[6];
    dev->m_info.size = *((uint32_t *)&buf[60]);
    
    if (dev->m_info.size == 0) {
        dev->m_info.size = dev->m_info.cylinders * dev->m_info.heads * dev->m_info.sectors;
    }
    
    dev->m_info.valid = 1;
    dev->m_present = 1;
    
    // Copy device name
    for (int i = 0; i < IDE_NAME_LEN; i++) {
        dev->m_name[i] = ide_configs[dev_id].name[i];
    }
    dev->m_name[IDE_NAME_LEN - 1] = '\0';
    
    // Re-enable interrupts for this device
    outb(ctrl, 0x00);
    
    return 0;
}

void hd_init(void) {
    // Enable IDE interrupts for both channels at PIC level
    pic_enable(IRQ_IDE1);
    pic_enable(IRQ_IDE2);

    // Try to detect all 4 possible devices
    for (int i = 0; i < MAX_IDE_DEVICES; i++) {
        ide_devices[i].detect(i);
    }
    
    cprintf("hd_init: found %d device(s)\n", num_devices);
}

/**
 * Read sectors from specific device (interrupt-driven)
 * @param dev_id: Device ID (0-3)
 * @param secno: Starting sector number
 * @param dst: Destination buffer
 * @param nsecs: Number of sectors
 * @return: 0 on success, -1 on failure
 */
int hd_read_device(int dev_id, uint32_t secno, void *dst, size_t nsecs) {
    IdeDevice *dev = &ide_devices[dev_id];
    if (!dev->m_present) {
        cprintf("hd_read: device %d not present\n", dev_id);
        return -1;
    }
    
    if (secno + nsecs > dev->m_info.size) {
        cprintf("hd_read: out of range (sector %d + %d > %d)\n", 
                secno, nsecs, dev->m_info.size);
        return -1;
    }
    
    uint16_t base = dev->m_base;
    uint8_t drive_sel = dev->m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Read sectors one by one (interrupt-driven)
    for (size_t i = 0; i < nsecs; i++) {
        uint32_t lba = secno + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(base) != 0) {
            cprintf("hd_read: device %s not ready\n", dev->m_name);
            return -1;
        }

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
        // Prepare transfer parameters
        dev->m_irq_done = 0;
        dev->m_err = 0;
        dev->m_buffer = (void *)((uint8_t *)dst + i * SECTOR_SIZE);
        dev->m_op = 1; // Read operation
        dev->m_waiting = current;

        // Set sector count and LBA address
        outb(base + IDE_SECTOR_COUNT, 1);
        outb(base + IDE_LBA_LOW, lba & 0xFF);
        outb(base + IDE_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(base + IDE_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

        // Send read command (will trigger interrupt)
        outb(base + IDE_COMMAND, IDE_CMD_READ);
        
        // cprintf("hd_read: command sent, waiting for interrupt on %s (lba=%d)\n", dev->name, lba);

        while (!dev->m_irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();

        // Clean up state
        dev->m_op = 0;
        dev->m_waiting = nullptr;

        // Check for errors
        if (dev->m_err) {
            cprintf("hd_read: error reading sector %d from %s\n", lba, dev->m_name);
            return -1;
        }
    }
    
    return 0;
}

/**
 * Write sectors to specific device (interrupt-driven)
 * @param dev_id: Device ID (0-3)
 * @param secno: Starting sector number
 * @param src: Source buffer
 * @param nsecs: Number of sectors
 * @return: 0 on success, -1 on failure
 */
int hd_write_device(int dev_id, uint32_t secno, const void *src, size_t nsecs) {
    IdeDevice *dev = &ide_devices[dev_id];
    if (!dev->m_present) {
        cprintf("hd_write: device %d not present\n", dev_id);
        return -1;
    }
    
    if (secno + nsecs > dev->m_info.size) {
        cprintf("hd_write: out of range (sector %d + %d > %d)\n", 
                secno, nsecs, dev->m_info.size);
        return -1;
    }
    
    uint16_t base = dev->m_base;
    uint8_t drive_sel = dev->m_drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Write sectors one by one (interrupt-driven)
    for (size_t i = 0; i < nsecs; i++) {
        uint32_t lba = secno + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(base) != 0) {
            cprintf("hd_write: device %s not ready\n", dev->m_name);
            return -1;
        }

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
        // Prepare transfer parameters
        dev->m_irq_done = 0;
        dev->m_err = 0;
        dev->m_buffer = (void *)((const uint8_t *)src + i * SECTOR_SIZE);
        dev->m_op = 2; // Write operation
        dev->m_waiting = current;

        // Set sector count and LBA address
        outb(base + IDE_SECTOR_COUNT, 1);
        outb(base + IDE_LBA_LOW, lba & 0xFF);
        outb(base + IDE_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(base + IDE_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

        // Send write command (device will trigger DRQ interrupt when ready for data)
        outb(base + IDE_COMMAND, IDE_CMD_WRITE);

        // TODO BEGIN

        // Wait for write to complete
        if (hd_wait_ready_on_base(base) != 0) {
            return -1;
        }

        // Write the data
        outsw(base + IDE_DATA, (const void *)dev->m_buffer, SECTOR_SIZE / 2);
        // dev->irq_done = 0;

        // TODO END

        while (!dev->m_irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();
        
        // Clean up state
        dev->m_op = 0;
        dev->m_waiting = nullptr;

        // Check for errors
        if (dev->m_err) {
            cprintf("hd_write: error writing sector %d to %s\n", lba, dev->m_name);
            return -1;
        }
    }
    
    return 0;
}

/**
 * Handle IDE interrupt (IRQ_IDE1 or IRQ_IDE2)
 * Called from trap handler to handle disk interrupts
 */
void hd_intr(int irq) {
    int channel = (irq == IRQ_IDE2) ? 1 : 0;

    // Iterate through all devices on this channel
    for (int i = 0; i < MAX_IDE_DEVICES; i++) {
        IdeDevice *dev = &ide_devices[i];
        
        // Skip devices not present or not on this channel
        if (!dev->m_present || dev->m_channel != channel) {
            continue;
        }

        // Read status register (clears interrupt)
        uint8_t status = inb(dev->m_base + IDE_STATUS);

        // cprintf("hd_intr: handling interrupt operator %d with status 0x%x for device %s on channel %d\n", dev->op, status, dev->name, channel);
        
        // If no operation is in progress, ignore interrupt
        if (dev->m_op == 0) {
            continue;
        }

        // Error handling
        if (status & IDE_ERR) {
            uint8_t err = inb(dev->m_base + IDE_ERROR);
            cprintf("hd_intr: disk error on %s (status=0x%02x, error=0x%02x)\n", 
                    dev->m_name, status, err);
            dev->m_err = -1;
            dev->m_irq_done = 1;
            if (dev->m_waiting) {
                wakeup_proc(dev->m_waiting);
            }
            continue;
        }

        // Read operation: read data when DRQ is set
        if (dev->m_op == 1) {
            if (!(status & IDE_DRQ)) {
                continue;
            }
            
            // Read one sector of data
            if (dev->m_buffer) {
                insw(dev->m_base + IDE_DATA, dev->m_buffer, SECTOR_SIZE / 2);
            }
            
            // Mark complete and wake waiting process
            dev->m_irq_done = 1;
            if (dev->m_waiting) {
                wakeup_proc(dev->m_waiting);
            }
            continue;
        }

        // Write operation: handle DRQ and completion signals
        if (dev->m_op == 2) {
            // If DRQ is set, device is ready to receive data
            if (status & IDE_DRQ) {
                if (dev->m_buffer) {
                    outsw(dev->m_base + IDE_DATA, dev->m_buffer, SECTOR_SIZE / 2);
                }
                // For simplicity, mark as done after writing data
                // Some devices may send another IRQ when truly complete
                dev->m_irq_done = 1;
                if (dev->m_waiting) {
                    wakeup_proc(dev->m_waiting);
                }
            } 
            // If both BSY and DRQ are clear, write operation is complete
            else if ((status & (IDE_BSY | IDE_DRQ)) == 0) {
                dev->m_irq_done = 1;
                if (dev->m_waiting) {
                    wakeup_proc(dev->m_waiting);
                }
            }
            continue;
        }
    }
}

/**
 * Get device by ID
 */
IdeDevice *hd_get_device(int dev_id) {
    if (!ide_devices[dev_id].m_present) {
        return nullptr;
    }
    
    return &ide_devices[dev_id];
}

/**
 * Get number of detected devices
 */
int hd_get_device_count(void) {
    return num_devices;
}

/**
 * Simple interrupt test - read one sector using polling first
 */
void hd_test_interrupt(void) {
    cprintf("\n=== IDE Interrupt Test ===\n");
    
    if (num_devices == 0) {
        cprintf("No devices available for testing\n");
        return;
    }
    
    IdeDevice *dev = &ide_devices[0];
    cprintf("Testing device: %s\n", dev->m_name);
    cprintf("  base=0x%x, ctrl=0x%x, irq=%d\n", dev->m_base, dev->m_ctrl, dev->m_irq);
    
    // Check interrupt enable status
    uint8_t ctrl = inb(dev->m_ctrl);
    cprintf("  Control register: 0x%02x (interrupts %s)\n", 
            ctrl, (ctrl & IDE_CTRL_nIEN) ? "DISABLED" : "ENABLED");
    
    // Check PIC mask
    cprintf("  Checking if IRQ %d is enabled in PIC...\n", dev->m_irq);
    
    // Try a simple read with interrupt
    static uint8_t buf[SECTOR_SIZE];
    cprintf("  Attempting interrupt-driven read of sector 0...\n");
    
    int result = hd_read_device(0, 0, buf, 1);
    if (result == 0) {
        cprintf("  SUCCESS: Read completed\n");
    } else {
        cprintf("  FAILED: Read failed\n");
    }
    
    cprintf("=== Test Complete ===\n\n");
}

/**
 * Test disk read/write for all devices
 */
void hd_test(void) {
    cprintf("\n=== Multi-Disk Test ===\n");
    cprintf("Testing %d disk device(s)\n\n", num_devices);
    
    if (num_devices == 0) {
        cprintf("No disk devices found!\n");
        cprintf("=== Test Complete ===\n\n");
        return;
    }
    
    // Allocate test buffers
    static uint8_t write_buf[SECTOR_SIZE];
    static uint8_t read_buf[SECTOR_SIZE];
    
    // Test each device
    for (int dev_id = 0; dev_id < MAX_IDE_DEVICES; dev_id++) {
        IdeDevice *dev = &ide_devices[dev_id];
        
        if (!dev->m_present) {
            continue;
        }
        
        cprintf("--- Testing %s (dev_id=%d) ---\n", dev->m_name, dev_id);
        cprintf("  Size: %d sectors (%d MB)\n", dev->m_info.size, dev->m_info.size / 2048);
        
        // Fill write buffer with test pattern (unique per device)
        for (int i = 0; i < SECTOR_SIZE; i++) {
            write_buf[i] = (uint8_t)((i + dev_id * 17) & 0xFF);
        }
        
        // Use sector 100 for testing
        uint32_t test_sector = 100;
        
        cprintf("  Test 1: Write sector %d...\n", test_sector);
        if (hd_write_device(dev_id, test_sector, write_buf, 1) != 0) {
            cprintf("    FAILED: write error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 2: Read sector %d...\n", test_sector);
        if (hd_read_device(dev_id, test_sector, read_buf, 1) != 0) {
            cprintf("    FAILED: read error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 3: Verify data...\n");
        int errors = 0;
        for (int i = 0; i < SECTOR_SIZE; i++) {
            if (read_buf[i] != write_buf[i]) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n", 
                           i, write_buf[i], read_buf[i]);
                }
                errors++;
            }
        }
        
        if (errors > 0) {
            cprintf("    FAILED: %d mismatches\n", errors);
        } else {
            cprintf("    OK\n");
        }
        
        cprintf("  %s test %s\n\n", dev->m_name, errors == 0 ? "PASSED" : "FAILED");
    }
    
    cprintf("=== Multi-Disk Test Complete ===\n\n");
}