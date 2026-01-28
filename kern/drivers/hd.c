#include "hd.h"
#include "stdio.h"

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>
#include <arch/x86/drivers/i8259.h>
#include "pic.h"
#include "../sched/sched.h"
#include "../drivers/intr.h"

// Global IDE devices
static ide_device_t ide_devices[MAX_IDE_DEVICES];
static int num_devices = 0;

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
    ide_device_t *dev = &ide_devices[dev_id];
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
    dev->channel = ide_configs[dev_id].channel;
    dev->drive = ide_configs[dev_id].drive;
    dev->base = base;
    dev->ctrl = ide_configs[dev_id].ctrl;
    dev->irq = ide_configs[dev_id].irq;
    dev->info.cylinders = buf[1];
    dev->info.heads = buf[3];
    dev->info.sectors = buf[6];
    dev->info.size = *((uint32_t *)&buf[60]);
    
    if (dev->info.size == 0) {
        dev->info.size = dev->info.cylinders * dev->info.heads * dev->info.sectors;
    }
    
    dev->info.valid = 1;
    dev->present = 1;
    
    // Copy device name
    for (int i = 0; i < IDE_NAME_LEN; i++) {
        dev->name[i] = ide_configs[dev_id].name[i];
    }
    dev->name[IDE_NAME_LEN - 1] = '\0';
    
    // Re-enable interrupts for this device
    outb(ctrl, 0x00);
    
    return 0;
}

void hd_init(void) {
    // Enable IDE interrupts for both channels at PIC level
    pic_enable(IRQ_IDE1);
    pic_enable(IRQ_IDE2);
    
    // Clear device array
    for (int i = 0; i < MAX_IDE_DEVICES; i++) {
        ide_devices[i].present = 0;
        ide_devices[i].info.valid = 0;
    }
    
    num_devices = 0;
    
    // Try to detect all 4 possible devices
    for (int i = 0; i < MAX_IDE_DEVICES; i++) {
        if (hd_detect_device(i) == 0) {
            num_devices++;
        }
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
    ide_device_t *dev = &ide_devices[dev_id];
    if (!dev->present) {
        cprintf("hd_read: device %d not present\n", dev_id);
        return -1;
    }
    
    if (secno + nsecs > dev->info.size) {
        cprintf("hd_read: out of range (sector %d + %d > %d)\n", 
                secno, nsecs, dev->info.size);
        return -1;
    }
    
    uint16_t base = dev->base;
    uint8_t drive_sel = dev->drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Read sectors one by one (interrupt-driven)
    for (size_t i = 0; i < nsecs; i++) {
        uint32_t lba = secno + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(base) != 0) {
            cprintf("hd_read: device %s not ready\n", dev->name);
            return -1;
        }

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
        // Prepare transfer parameters
        dev->irq_done = 0;
        dev->err = 0;
        dev->buffer = (void *)((uint8_t *)dst + i * SECTOR_SIZE);
        dev->op = 1; // Read operation
        dev->waiting = current;

        // Set sector count and LBA address
        outb(base + IDE_SECTOR_COUNT, 1);
        outb(base + IDE_LBA_LOW, lba & 0xFF);
        outb(base + IDE_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + IDE_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(base + IDE_DEVICE, drive_sel | ((lba >> 24) & 0x0F));

        // Send read command (will trigger interrupt)
        outb(base + IDE_COMMAND, IDE_CMD_READ);
        
        // cprintf("hd_read: command sent, waiting for interrupt on %s (lba=%d)\n", dev->name, lba);

        while (!dev->irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();

        // Clean up state
        dev->op = 0;
        dev->waiting = NULL;

        // Check for errors
        if (dev->err) {
            cprintf("hd_read: error reading sector %d from %s\n", lba, dev->name);
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
    ide_device_t *dev = &ide_devices[dev_id];
    if (!dev->present) {
        cprintf("hd_write: device %d not present\n", dev_id);
        return -1;
    }
    
    if (secno + nsecs > dev->info.size) {
        cprintf("hd_write: out of range (sector %d + %d > %d)\n", 
                secno, nsecs, dev->info.size);
        return -1;
    }
    
    uint16_t base = dev->base;
    uint8_t drive_sel = dev->drive ? IDE_DEV_SLAVE : IDE_DEV_MASTER;
    
    // Write sectors one by one (interrupt-driven)
    for (size_t i = 0; i < nsecs; i++) {
        uint32_t lba = secno + i;

        // Wait for device ready
        if (hd_wait_ready_on_base(base) != 0) {
            cprintf("hd_write: device %s not ready\n", dev->name);
            return -1;
        }

        // Disable interrupts before issuing command, prepare transfer descriptor
        intr_save();
        
        // Prepare transfer parameters
        dev->irq_done = 0;
        dev->err = 0;
        dev->buffer = (void *)((const uint8_t *)src + i * SECTOR_SIZE);
        dev->op = 2; // Write operation
        dev->waiting = current;

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
        outsw(base + IDE_DATA, (const void *)dev->buffer, SECTOR_SIZE / 2);
        // dev->irq_done = 0;

        // TODO END

        while (!dev->irq_done) {
            current->state = TASK_SLEEPING;
            intr_restore();
            schedule();
            intr_save();
        }

        intr_restore();
        
        // Clean up state
        dev->op = 0;
        dev->waiting = NULL;

        // Check for errors
        if (dev->err) {
            cprintf("hd_write: error writing sector %d to %s\n", lba, dev->name);
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
        ide_device_t *dev = &ide_devices[i];
        
        // Skip devices not present or not on this channel
        if (!dev->present || dev->channel != channel) {
            continue;
        }

        // Read status register (clears interrupt)
        uint8_t status = inb(dev->base + IDE_STATUS);

        // cprintf("hd_intr: handling interrupt operator %d with status 0x%x for device %s on channel %d\n", dev->op, status, dev->name, channel);
        
        // If no operation is in progress, ignore interrupt
        if (dev->op == 0) {
            continue;
        }

        // Error handling
        if (status & IDE_ERR) {
            uint8_t err = inb(dev->base + IDE_ERROR);
            cprintf("hd_intr: disk error on %s (status=0x%02x, error=0x%02x)\n", 
                    dev->name, status, err);
            dev->err = -1;
            dev->irq_done = 1;
            if (dev->waiting) {
                wakeup_proc(dev->waiting);
            }
            continue;
        }

        // Read operation: read data when DRQ is set
        if (dev->op == 1) {
            if (!(status & IDE_DRQ)) {
                continue;
            }
            
            // Read one sector of data
            if (dev->buffer) {
                insw(dev->base + IDE_DATA, dev->buffer, SECTOR_SIZE / 2);
            }
            
            // Mark complete and wake waiting process
            dev->irq_done = 1;
            if (dev->waiting) {
                wakeup_proc(dev->waiting);
            }
            continue;
        }

        // Write operation: handle DRQ and completion signals
        if (dev->op == 2) {
            // If DRQ is set, device is ready to receive data
            if (status & IDE_DRQ) {
                if (dev->buffer) {
                    outsw(dev->base + IDE_DATA, dev->buffer, SECTOR_SIZE / 2);
                }
                // For simplicity, mark as done after writing data
                // Some devices may send another IRQ when truly complete
                dev->irq_done = 1;
                if (dev->waiting) {
                    wakeup_proc(dev->waiting);
                }
            } 
            // If both BSY and DRQ are clear, write operation is complete
            else if ((status & (IDE_BSY | IDE_DRQ)) == 0) {
                dev->irq_done = 1;
                if (dev->waiting) {
                    wakeup_proc(dev->waiting);
                }
            }
            continue;
        }
    }
}

/**
 * Get device by ID
 */
ide_device_t *hd_get_device(int dev_id) {
    if (!ide_devices[dev_id].present) {
        return NULL;
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
    
    ide_device_t *dev = &ide_devices[0];
    cprintf("Testing device: %s\n", dev->name);
    cprintf("  base=0x%x, ctrl=0x%x, irq=%d\n", dev->base, dev->ctrl, dev->irq);
    
    // Check interrupt enable status
    uint8_t ctrl = inb(dev->ctrl);
    cprintf("  Control register: 0x%02x (interrupts %s)\n", 
            ctrl, (ctrl & IDE_CTRL_nIEN) ? "DISABLED" : "ENABLED");
    
    // Check PIC mask
    cprintf("  Checking if IRQ %d is enabled in PIC...\n", dev->irq);
    
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
        ide_device_t *dev = &ide_devices[dev_id];
        
        if (!dev->present) {
            continue;
        }
        
        cprintf("--- Testing %s (dev_id=%d) ---\n", dev->name, dev_id);
        cprintf("  Size: %d sectors (%d MB)\n", dev->info.size, dev->info.size / 2048);
        
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
        
        cprintf("  %s test %s\n\n", dev->name, errors == 0 ? "PASSED" : "FAILED");
    }
    
    cprintf("=== Multi-Disk Test Complete ===\n\n");
}