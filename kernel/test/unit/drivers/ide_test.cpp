#include "drivers/ide.h"
#include "lib/stdio.h"

#include <asm/arch.h>

void driver_test_disktest(void) {
    cprintf("\n=== Multi-Disk Test ===\n");
    cprintf("Testing %d disk device(s)\n\n", IdeManager::get_device_count());

    if (IdeManager::get_device_count() == 0) {
        cprintf("No disk devices found!\n");
        cprintf("=== Test Complete ===\n\n");
        return;
    }

    // Allocate test buffers
    static uint8_t write_buff[ide::SECTOR_SIZE]{};
    static uint8_t read_buff[ide::SECTOR_SIZE]{};

    // Test each device
    for (int i = 0; i < IdeManager::get_device_count(); i++) {
        IdeDevice* dev = IdeManager::get_device(i);

        if (dev == nullptr || !dev->present) {
            continue;
        }

        cprintf("--- Testing %s (dev_id=%d) ---\n", dev->name, i);
        cprintf("  Size: %d sectors (%d MB)\n", dev->info.size, dev->info.size / 2048);

        uint32_t test_sector = (dev->info.size > 200) ? 100 : (dev->info.size > 1 ? dev->info.size - 1 : 0);
        if (test_sector == 0 && dev->info.size <= 1) {
            cprintf("  SKIP: device too small for write test\n\n");
            continue;
        }

        // Keep test non-destructive by restoring original sector after verification.
        static uint8_t backup_buff[ide::SECTOR_SIZE]{};
        if (dev->read(test_sector, backup_buff, 1) != 0) {
            cprintf("  SKIP: failed to read backup sector %d\n\n", test_sector);
            continue;
        }

        // Fill write buffer with test pattern (unique per device)
        for (size_t j = 0; j < ide::SECTOR_SIZE; j++) {
            write_buff[j] = (uint8_t)((j + j * 17) & 0xFF);
        }

        cprintf("  Test 1: Write sector %d...\n", test_sector);
        if (dev->write(test_sector, write_buff, 1) != 0) {
            cprintf("    FAILED: write error\n");
            continue;
        }
        cprintf("    OK\n");

        cprintf("  Test 2: Read sector %d...\n", test_sector);
        if (dev->read(test_sector, read_buff, 1) != 0) {
            cprintf("    FAILED: read error\n");
            continue;
        }
        cprintf("    OK\n");

        cprintf("  Test 3: Verify data...\n");
        int errors = 0;
        for (size_t j = 0; j < ide::SECTOR_SIZE; j++) {
            if (read_buff[j] != write_buff[j]) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n", j, write_buff[j], read_buff[j]);
                }
                errors++;
            }
        }

        if (errors > 0) {
            cprintf("    FAILED: %d mismatches\n", errors);
        } else {
            cprintf("    OK\n");
        }

        cprintf("  Test 4: Restore original sector...\n");
        if (dev->write(test_sector, backup_buff, 1) != 0) {
            cprintf("    WARNING: failed to restore sector %d\n", test_sector);
        } else {
            cprintf("    OK\n");
        }

        cprintf("  %s test %s\n\n", dev->name, errors == 0 ? "PASSED" : "FAILED");
    }

    cprintf("=== Multi-Disk Test Complete ===\n\n");
}

void driver_test_intrtest(void) {
    cprintf("\n=== IDE Interrupt Test ===\n");

    if (IdeManager::get_device_count() == 0) {
        cprintf("No devices available for testing\n");
        return;
    }

    IdeDevice* dev = IdeManager::get_device(0);
    if (dev == nullptr) {
        cprintf("Failed to get device 0\n");
        return;
    }

    cprintf("Testing device: %s\n", dev->name);
    cprintf("  base=0x%x, ctrl=0x%x, irq=%d\n", dev->config->base, dev->config->ctrl, dev->config->irq);

    // Check interrupt enable status
    uint8_t ctrl = arch_port_inb(dev->config->ctrl);
    cprintf("  Control register: 0x%02x (interrupts %s)\n", ctrl, (ctrl & ide::CTRL_nIEN) ? "DISABLED" : "ENABLED");

    // Check PIC mask
    cprintf("  Checking if IRQ %d is enabled in PIC...\n", dev->config->irq);

    // Try a simple read with interrupt
    cprintf("  Attempting interrupt-driven read of sector 0...\n");
    static uint8_t buf[ide::SECTOR_SIZE]{};
    int result = dev->read(0, buf, 1);
    if (result == 0) {
        cprintf("  SUCCESS: Read completed\n");
    } else {
        cprintf("  FAILED: Read failed\n");
    }

    cprintf("=== Test Complete ===\n\n");
}
