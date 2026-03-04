#pragma once

#include <base/types.h>

// Block device constants
namespace blk {

// Block device types
enum class DeviceType : uint8_t {
    None = 0,
    Disk = 1,  // Hard disk
    Swap = 2,  // Swap device
};

}  // namespace blk

struct BlockDevice {
    static constexpr size_t SIZE = 512;  // Standard block size (sector size)

    blk::DeviceType type{};  // Device type
    uint32_t size{};         // Size in blocks
    char name[8]{};          // Device name

    // Virtual operations (for C++ subclasses)
    virtual int read(uint32_t block_number, void* buf, size_t block_count) = 0;
    virtual int write(uint32_t block_number, const void* buf, size_t block_count) = 0;
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
    static void print();

private:
    static BlockDevice* s_devices[MAX_DEV];
    static int s_device_count;
};

namespace blk {

void init();

}  // namespace blk
