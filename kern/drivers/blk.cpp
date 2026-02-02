#include "blk.h"
#include "hd.h"
#include "stdio.h"
#include "string.h"

// Static member definitions
BlockDevice* BlockDevice::s_devices[MAX_BLK_DEV] = {};
int BlockDevice::s_device_count = 0;

void BlockDevice::init() {
    cprintf("blk_init: initializing block device layer...\n");

    IdeDevice::init();
    
    int hdCount = IdeDevice::get_device_count();
    for (int i = 0; i < hdCount; i++) {
        IdeDevice *ideDevice = IdeDevice::get_device(i);
        if (ideDevice) {
            ideDevice->m_size = ideDevice->m_info.size;
            ideDevice->register_device();
        }
    }
    
    if (hdCount == 0) {
        cprintf("blk_init: no disk devices found\n");
    }
}

void BlockDevice::register_device() {
    if (s_device_count >= MAX_BLK_DEV) {
        cprintf("BlockDevice::register_device: too many devices\n");
        return;
    }
    
    s_devices[s_device_count++] = this;
}

BlockDevice* BlockDevice::get_device(const char* deviceName) {
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

BlockDevice* BlockDevice::get_device(int index) {
    if (index < 0 || index >= s_device_count) {
        return nullptr;
    }
    return s_devices[index];
}

int BlockDevice::get_device_count() {
    return s_device_count;
}

void BlockDevice::print_info() {
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");
    
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i]) {
            const char *typeString = "disk";
            const char *mountString = "";
            
            if (s_devices[i]->m_type == BLK_TYPE_SWAP) {
                mountString = "[SWAP]";
            }
            
            uint32_t sizeBytes = s_devices[i]->m_size * BLK_SIZE;
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
