#pragma once

#include <base/types.h>

// Block device constants
namespace blk {

inline constexpr size_t SIZE        = 512;  // Standard block size (sector size)
inline constexpr int MAX_DEV        = 4;    // Maximum number of block devices

// Block device types
inline constexpr int TYPE_DISK = 1;         // Hard disk
inline constexpr int TYPE_SWAP = 2;         // Swap device

} // namespace blk

// Legacy compatibility
#define BLK_SIZE      blk::SIZE
#define MAX_BLK_DEV   blk::MAX_DEV
#define BLK_TYPE_DISK blk::TYPE_DISK
#define BLK_TYPE_SWAP blk::TYPE_SWAP

// Block device operations
struct BlockDevice {
    int type{};                           // Device type
    uint32_t size{};                      // Size in blocks
    const char* name{};                   // Device name
    void* private_data{};                 // Private data (e.g., device ID)
    
    // Operations
    int (*read)(BlockDevice* dev, uint32_t blockno, void* buf, size_t nblocks);
    int (*write)(BlockDevice* dev, uint32_t blockno, const void* buf, size_t nblocks);
};

// Block device management functions
void blk_init();
int blk_register(BlockDevice* dev);
int blk_get_device_count();

BlockDevice* blk_get_device(int type);
BlockDevice* blk_get_device_by_name(const char* name);
BlockDevice* blk_get_device_by_index(int index);

int blk_read(BlockDevice* dev, uint32_t blockno, void* buf, size_t nblocks);
int blk_write(BlockDevice* dev, uint32_t blockno, const void* buf, size_t nblocks);
void blk_list_devices();
