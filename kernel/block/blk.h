#pragma once

#include <base/types.h>

// Block device constants
namespace blk {

// Block device types
enum class DeviceType {
    None = 0,
    Disk = 1,           // Hard disk
    Swap = 2,           // Swap device
};

} // namespace blk

// Block device operations
struct BlockDevice {
    static constexpr size_t SIZE = 512;  // Standard block size (sector size)

    blk::DeviceType m_type{};               // Device type
    uint32_t m_size{};                      // Size in blocks
    char m_name[8]{};                       // Device name
    
    // Virtual operations (for C++ subclasses)
    virtual int read(uint32_t blockNumber, void* buf, size_t blockCount) = 0;
    virtual int write(uint32_t blockNumber, const void* buf, size_t blockCount) = 0;
};

// Block device manager
class BlockManager {
public:
    static constexpr int MAX_DEV = 4;

    static void init();
    static void register_device(BlockDevice* device);
    static BlockDevice* get_device(const char* name);
    static BlockDevice* get_device(int index);
    static BlockDevice* get_device(blk::DeviceType type);
    static int get_device_count();
    static void print_info();

private:
    static BlockDevice* s_devices[MAX_DEV];
    static int s_device_count;
};

namespace blk {
    void init();
}
