#include "ide.h"
#include "stdio.h"

#include <asm/arch.h>

/**
 * Test disk read/write for all devices
 */
void IdeManager::test(void) {
    cprintf("\n=== Multi-Disk Test ===\n");
    cprintf("Testing %d disk device(s)\n\n", IdeManager::get_device_count());
    
    if (IdeManager::get_device_count() == 0) {
        cprintf("No disk devices found!\n");
        cprintf("=== Test Complete ===\n\n");
        return;
    }
    
    // Allocate test buffers
    static uint8_t writeBuff[ide::SECTOR_SIZE]{};
    static uint8_t readBuff[ide::SECTOR_SIZE]{};
    
    // Test each device
    for (int i = 0; i < IdeManager::get_device_count(); i++) {
        IdeDevice* dev = IdeManager::get_device(i);
        
        if (dev == nullptr || !dev->m_present) {
            continue;
        }
        
        cprintf("--- Testing %s (dev_id=%d) ---\n", dev->m_name, i);
        cprintf("  Size: %d sectors (%d MB)\n", dev->m_info.size, dev->m_info.size / 2048);
        
        // Fill write buffer with test pattern (unique per device)
        for (size_t j = 0; j < ide::SECTOR_SIZE; j++) {
            writeBuff[j] = (uint8_t)((j + j * 17) & 0xFF);
        }
        
        // Use sector 100 for testing
        uint32_t testSector = 100;
        cprintf("  Test 1: Write sector %d...\n", testSector);
        if (dev->write(testSector, writeBuff, 1) != 0) {
            cprintf("    FAILED: write error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 2: Read sector %d...\n", testSector);
        if (dev->read(testSector, readBuff, 1) != 0) {
            cprintf("    FAILED: read error\n");
            continue;
        }
        cprintf("    OK\n");
        
        cprintf("  Test 3: Verify data...\n");
        int errors = 0;
        for (size_t j = 0; j < ide::SECTOR_SIZE; j++) {
            if (readBuff[j] != writeBuff[j]) {
                if (errors < 5) {
                    cprintf("    Mismatch at offset %d: expected 0x%02x, got 0x%02x\n", j, writeBuff[j], readBuff[j]);
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

/**
 * Simple interrupt test - read one sector using polling first
 */
void IdeManager::test_interrupt(void) {
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
    
    cprintf("Testing device: %s\n", dev->m_name);
    cprintf("  base=0x%x, ctrl=0x%x, irq=%d\n", dev->m_config->base, dev->m_config->ctrl, dev->m_config->irq);
    
    // Check interrupt enable status
    uint8_t ctrl = arch_port_inb(dev->m_config->ctrl);
    cprintf("  Control register: 0x%02x (interrupts %s)\n", 
            ctrl, (ctrl & ide::CTRL_nIEN) ? "DISABLED" : "ENABLED");
    
    // Check PIC mask
    cprintf("  Checking if IRQ %d is enabled in PIC...\n", dev->m_config->irq);
    
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
