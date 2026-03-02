#include "blk.h"

#include "stdio.h"
#include "string.h"

#include <asm/drivers/i8259.h>

#include "../drivers/pic.h"
#include "../drivers/ide.h"
#include "../drivers/ahci.h"

// Static member definitions
BlockDevice* BlockManager::s_devices[BlockManager::MAX_DEV] = {};
int BlockManager::s_device_count = 0;

void BlockManager::init() {
    cprintf("blk_init: initializing block device layer...\n");

    pic::enable(IRQ_IDE1);
    pic::enable(IRQ_IDE2);

    IdeManager::init();
    int hd_count = IdeManager::get_device_count();
    for (int i = 0; i < hd_count; i++) {
        IdeDevice *ide_device = IdeManager::get_device(i);
        if (ide_device) {
            ide_device->m_size = ide_device->m_info.size;
            register_device(ide_device);
        }
    }
    
    if (hd_count == 0) {
        cprintf("blk_init: no IDE disk devices found\n");
    }
    
    // Initialize AHCI devices
    AhciManager::init();
    int ahci_count = AhciManager::get_device_count();
    for (int i = 0; i < ahci_count; i++) {
        AhciDevice *ahci_device = AhciManager::get_device(i);
        if (ahci_device) {
            ahci_device->m_size = ahci_device->m_info.size;
            register_device(ahci_device);
        }
    }
    
    if (ahci_count == 0) {
        cprintf("blk_init: no AHCI disk devices found\n");
    }
}

void BlockManager::register_device(BlockDevice* device) {
    if (s_device_count >= MAX_DEV) {
        cprintf("BlockManager::register_device: too many devices\n");
        return;
    }
    
    s_devices[s_device_count++] = device;
}

BlockDevice* BlockManager::get_device(const char* device_name) {
    if (!device_name) {
        return nullptr;
    }
    
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] && strcmp(s_devices[i]->m_name, device_name) == 0) {
            return s_devices[i];
        }
    }
    return nullptr;
}

BlockDevice* BlockManager::get_device(int index) {
    if (index < 0 || index >= s_device_count) {
        return nullptr;
    }
    return s_devices[index];
}

BlockDevice* BlockManager::get_device(blk::DeviceType type) {
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] && s_devices[i]->m_type == type) {
            return s_devices[i];
        }
    }
    return nullptr;
}

int BlockManager::get_device_count() {
    return s_device_count;
}

void BlockManager::print_info() {
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");
    
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i]) {
            const char *type_string = "disk";
            const char *mount_string = "";
            
            if (s_devices[i]->m_type == blk::DeviceType::Swap) {
                mount_string = "[SWAP]";
            }
            
            uint32_t size_bytes = s_devices[i]->m_size * BlockDevice::SIZE;
            uint32_t size_mb = size_bytes / (1024 * 1024);
            uint32_t remainder = size_bytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);
            
            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n",
                   s_devices[i]->m_name,
                   8,
                   i * 16,
                   0,
                   size_mb,
                   decimal,
                   0,
                   type_string,
                   mount_string);
        }
    }
}

namespace blk {

void init() {
    BlockManager::init();
}

}
