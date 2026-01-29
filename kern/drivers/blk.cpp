#include "blk.h"
#include "hd.h"
#include "stdio.h"

// Block device registry
static block_device_t *block_devices[MAX_BLK_DEV]{};
static block_device_t disk_devs[MAX_BLK_DEV]{};
static int num_devices{};

static int disk_read_wrapper(block_device_t* dev, uint32_t blockno, void* buf, size_t nblocks);
static int disk_write_wrapper(block_device_t* dev, uint32_t blockno, const void* buf, size_t nblocks);

void blk_init(void) {
    cprintf("blk_init: initializing block device layer...\n");

    hd_init();

    int num_hd = hd_get_device_count();
    for (int i = 0; i < num_hd && i < MAX_BLK_DEV; i++) {
        ide_device_t *ide_dev = hd_get_device(i);
        if (ide_dev && ide_dev->present) {
            disk_devs[i].type = BLK_TYPE_DISK;
            disk_devs[i].name = ide_dev->name;
            disk_devs[i].size = ide_dev->info.size;
            disk_devs[i].read = disk_read_wrapper;
            disk_devs[i].write = disk_write_wrapper;
            disk_devs[i].private_data = (void *)(long)i;  // Store device ID
            
            blk_register(&disk_devs[i]);
        }
    }
    
    if (num_hd == 0) {
        cprintf("blk_init: no disk devices found\n");
    }
}

int blk_register(block_device_t* dev) {
    if (num_devices >= MAX_BLK_DEV) {
        cprintf("blk_register: too many devices\n");
        return -1;
    }
    
    block_devices[num_devices++] = dev;
    return 0;
}

int blk_get_device_count(void) {
    return num_devices;
}

/**
 * Get a block device by type
 */
block_device_t* blk_get_device(int type) {
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i] && block_devices[i]->type == type) {
            return block_devices[i];
        }
    }
    return nullptr;
}

/**
 * Get a block device by name
 */
block_device_t* blk_get_device_by_name(const char *name) {
    if (!name) {
        return nullptr;
    }
    
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i] && block_devices[i]->name) {
            // Simple string comparison
            const char *p1 = block_devices[i]->name;
            const char *p2 = name;
            while (*p1 && *p2 && *p1 == *p2) {
                p1++;
                p2++;
            }
            if (*p1 == *p2) {  // Both reached end
                return block_devices[i];
            }
        }
    }
    return nullptr;
}

block_device_t* blk_get_device_by_index(int index) {
    if (index < 0 || index >= num_devices) {
        return nullptr;
    }
    return block_devices[index];
}

/**
 * Read blocks from a device
 */
int blk_read(block_device_t* dev, uint32_t blockno, void* buf, size_t nblocks) {
    if (dev == nullptr || dev->read == nullptr) {
        return -1;
    }
    
    return dev->read(dev, blockno, buf, nblocks);
}

/**
 * Write blocks to a device
 */
int blk_write(block_device_t* dev, uint32_t blockno, const void *buf, size_t nblocks) {
    if (dev == nullptr || dev->write == nullptr) {
        return -1;
    }
    
    return dev->write(dev, blockno, buf, nblocks);
}

/**
 * List all registered block devices (Linux lsblk style)
 */
void blk_list_devices(void) {
    // Print header
    cprintf("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");
    
    for (int i = 0; i < num_devices; i++) {
        if (block_devices[i]) {
            const char *type_str = "disk";
            const char *mount_str = "";
            
            if (block_devices[i]->type == BLK_TYPE_SWAP) {
                type_str = "disk";
                mount_str = "[SWAP]";
            }
            
            // Calculate size in bytes
            uint32_t size_bytes = block_devices[i]->size * BLK_SIZE;
            
            // Calculate size with one decimal place
            // size_mb = size_bytes / (1024 * 1024)
            // decimal = (size_bytes % (1024 * 1024)) * 10 / (1024 * 1024)
            uint32_t size_mb = size_bytes / (1024 * 1024);
            uint32_t remainder = size_bytes % (1024 * 1024);
            uint32_t decimal = (remainder * 10) / (1024 * 1024);
            
            // Format: NAME   MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
            // Now cprintf supports left-align with '-' flag
            cprintf("%-6s %3d:%-3d %-2d %2d.%dM %-2d %-4s %s\n",
                   block_devices[i]->name,  // NAME (left-aligned, 6 chars)
                   8,                        // MAJ (SCSI disk major number)
                   i * 16,                   // MIN (minor number)
                   0,                        // RM (removable: 0=no, 1=yes)
                   size_mb,                  // SIZE integer part
                   decimal,                  // SIZE decimal part (one digit)
                   0,                        // RO (read-only: 0=no, 1=yes)
                   type_str,                 // TYPE (left-aligned, 4 chars)
                   mount_str);               // MOUNTPOINTS
        }
    }
}

/**
 * Wrapper functions for disk operations
 */
static int disk_read_wrapper(block_device_t *dev, uint32_t blockno, void *buf, size_t nblocks) {
    int dev_id = (int)(long)dev->private_data;
    return hd_read_device(dev_id, blockno, buf, nblocks);
}

static int disk_write_wrapper(block_device_t *dev, uint32_t blockno, const void *buf, size_t nblocks) {
    int dev_id = (int)(long)dev->private_data;
    return hd_write_device(dev_id, blockno, buf, nblocks);
}
