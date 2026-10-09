#pragma once

#include <base/types.h>
#include <sys/inplace_vector.hpp>
#include "lib/result.h"

namespace blk {

enum class DeviceType : uint8_t {
    None = 0,
    Disk = 1,  // Hard disk
    Swap = 2,  // Swap device
};

}  // namespace blk

struct BlockDevice {
    static constexpr size_t BLOCK_SIZE_BYTES = 512;

    blk::DeviceType type{};  // Device type
    uint32_t block_count{};  // Size in blocks
    char name[8]{};          // Device name

    // start_lba is the first logical block; block_count is the transfer length.
    virtual Error read(uint32_t start_lba, void* buf, size_t block_count) = 0;
    virtual Error write(uint32_t start_lba, const void* buf, size_t block_count) = 0;
    virtual void print_info();
};

class BlockManager {
public:
    static constexpr int MAX_DEVICES = 4;

    static void init();
    static Error register_device(BlockDevice* device);
    static Error register_devices(BlockDevice* const* devices, size_t count);
    static BlockDevice* find_device(const char* name);
    static BlockDevice* find_device(int index);
    static BlockDevice* find_device(blk::DeviceType type);
    static int device_count();
    static void print();


private:
    inline static sys::inplace_vector<BlockDevice*, MAX_DEVICES> devices_{};
};

namespace blk {

int init();

// Register architecture/platform-specific block backends
int probe_backends();
Error register_device(BlockDevice* device);

}  // namespace blk
