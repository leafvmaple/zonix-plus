#include "blk.h"
#include "hd.h"
#include "stdio.h"
#include "string.h"

// Static member definitions
BlockDevice* BlockManager::s_devices[BlockManager::MAX_DEV] = {};
int BlockManager::s_device_count = 0;

void BlockManager::init() {
    cprintf("blk_init: initializing block device layer...\n");

    IdeManager::init();
    
    int hdCount = IdeManager::get_device_count();
    for (int i = 0; i < hdCount; i++) {
        IdeDevice *ideDevice = IdeManager::get_device(i);
        if (ideDevice) {
            ideDevice->m_size = ideDevice->m_info.size;
            BlockManager::register_device(ideDevice);
        }
    }
    
    if (hdCount == 0) {
        cprintf("blk_init: no disk devices found\n");
    }
}

void BlockManager::register_device(BlockDevice* device) {
    if (s_device_count >= MAX_DEV) {
        cprintf("BlockManager::register_device: too many devices\n");
        return;
    }
    
    s_devices[s_device_count++] = device;
}

BlockDevice* BlockManager::get_device(const char* deviceName) {
    if (!deviceName) {
        return nullptr;
    }
    
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i]) {
            if (strcmp(s_devices[i]->m_name, deviceName) == 0) {
                return s_devices[i];
            }
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
            const char *typeString = "disk";
            const char *mountString = "";
            
            if (s_devices[i]->m_type == blk::DeviceType::Swap) {
                mountString = "[SWAP]";
            }
            
            uint32_t sizeBytes = s_devices[i]->m_size * BlockDevice::SIZE;
            uint32_t sizeMB = sizeBytes / (1024 * 1024);
            uint32_t remainder = sizeBytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);
            
            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n",
                   s_devices[i]->m_name,
                   8,
                   i * 16,
                   0,
                   sizeMB,
                   decimal,
                   0,
                   typeString,
                   mountString);
        }
    }
}

namespace blk {
    void init() {
        BlockManager::init();
    }
}
