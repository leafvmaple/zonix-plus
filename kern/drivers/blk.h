#pragma once

#include <base/types.h>

// Block device constants
#define BLK_SIZE        512             // Standard block size (sector size)
#define MAX_BLK_DEV     4               // Maximum number of block devices

// Block device types
#define BLK_TYPE_DISK   1               // Hard disk
#define BLK_TYPE_SWAP   2               // Swap device

// Block device operations
struct block_device_t {
    int type{};                           // Device type
    uint32_t size{};                      // Size in blocks
    const char* name{};                   // Device name
    void* private_data{};                 // Private data (e.g., device ID)
    
    // Operations
    int (*read)(struct block_device_t* dev, uint32_t blockno, void* buf, size_t nblocks);
    int (*write)(struct block_device_t* dev, uint32_t blockno, const void* buf, size_t nblocks);
};

// Block device management functions
void blk_init(void);
int blk_register(block_device_t* dev);
int blk_get_device_count(void);

block_device_t* blk_get_device(int type);
block_device_t* blk_get_device_by_name(const char* name);
block_device_t* blk_get_device_by_index(int index);

int blk_read(block_device_t* dev, uint32_t blockno, void* buf, size_t nblocks);
int blk_write(block_device_t* dev, uint32_t blockno, const void* buf, size_t nblocks);
void blk_list_devices(void);
