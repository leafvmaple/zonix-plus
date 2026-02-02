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
    int m_type{};                           // Device type
    uint32_t m_size{};                      // Size in blocks
    char m_name[8]{};                       // Device name
    
    // Virtual operations (for C++ subclasses)
    virtual int read(uint32_t blockNumber, void* buf, size_t blockCount) = 0;
    virtual int write(uint32_t blockNumber, const void* buf, size_t blockCount) = 0;

    void register_device();

    static void init();
    static BlockDevice* get_device(const char* name);
    static BlockDevice* get_device(int index);
    static int get_device_count();
    static void print_info();

private:
    static BlockDevice* s_devices[MAX_BLK_DEV];
    static int s_device_count;
};



