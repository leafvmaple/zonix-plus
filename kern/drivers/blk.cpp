#include "blk.h"
#include "hd.h"
#include "stdio.h"

// Static member definitions
BlockDevice* BlockDevice::s_devices[MAX_BLK_DEV] = {};
int BlockDevice::s_device_count = 0;

void blk_init(void) {
    cprintf("blk_init: initializing block device layer...\n");

    hd_init();

    int num_hd = hd_get_device_count();
    for (int i = 0; i < num_hd && i < MAX_BLK_DEV; i++) {
        IdeDevice *ide_dev = hd_get_device(i);
        if (ide_dev && ide_dev->m_present) {
            // IdeDevice already inherits from BlockDevice
            ide_dev->type = BLK_TYPE_DISK;
            ide_dev->name = ide_dev->m_name;
            ide_dev->size = ide_dev->m_info.size;
            
            blk_register(ide_dev);
        }
    }
    
    if (num_hd == 0) {
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

BlockDevice* BlockDevice::get_device(const char* dev_name) {
    if (!dev_name) {
        return nullptr;
    }
    
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i] && s_devices[i]->name) {
            const char *p1 = s_devices[i]->name;
            const char *p2 = dev_name;
            while (*p1 && *p2 && *p1 == *p2) {
                p1++;
                p2++;
            }
            if (*p1 == *p2) {
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

// Legacy API wrappers
static BlockDevice *block_devices[MAX_BLK_DEV] = {};
static int num_devices = 0;

int blk_register(BlockDevice* dev) {
    if (num_devices >= MAX_BLK_DEV) {
        cprintf("blk_register: too many devices\n");
        return -1;
    }
    
    block_devices[num_devices++] = dev;
    dev->register_device();
    return 0;
}

int blk_get_device_count(void) {
    return num_devices;
}

BlockDevice* blk_get_device(int type) {
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i] && block_devices[i]->type == type) {
            return block_devices[i];
        }
    }
    return nullptr;
}

BlockDevice* blk_get_device_by_name(const char *dev_name) {
    if (!dev_name) {
        return nullptr;
    }
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i] && block_devices[i]->name) {
            const char *p1 = block_devices[i]->name;
            const char *p2 = dev_name;
            while (*p1 && *p2 && *p1 == *p2) {
                p1++;
                p2++;
            }
            if (*p1 == *p2) {
                return block_devices[i];
            }
        }
    }
    return nullptr;
}

BlockDevice* blk_get_device_by_index(int index) {
    if (index < 0 || index >= num_devices) {
        return nullptr;
    }
    return block_devices[index];
}

int blk_read(BlockDevice* dev, uint32_t blockno, void* buf, size_t nblocks) {
    if (dev == nullptr) {
        return -1;
    }
    
    return dev->read(blockno, buf, nblocks);
}

int blk_write(BlockDevice* dev, uint32_t blockno, const void *buf, size_t nblocks) {
    if (dev == nullptr) {
        return -1;
    }
    
    return dev->write(blockno, buf, nblocks);
}

void blk_list_devices(void) {
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");
    
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i]) {
            const char *type_str = "disk";
            const char *mount_str = "";
            
            if (block_devices[i]->type == BLK_TYPE_SWAP) {
                type_str = "disk";
                mount_str = "[SWAP]";
            }
            
            uint32_t size_bytes = block_devices[i]->size * BLK_SIZE;
            uint32_t size_mb = size_bytes / (1024 * 1024);
            uint32_t remainder = size_bytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);
            
            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n",
                   block_devices[i]->name,
                   8,
                   i * 16,
                   0,
                   size_mb,
                   decimal,
                   0,
                   type_str,
                   mount_str);
        }
    }
}
