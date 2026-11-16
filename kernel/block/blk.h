#pragma once

#include <base/types.h>
#include <debug/assert.h>
#include <sys/inplace_vector.hpp>
#include "lib/result.h"
#include "lib/activity_gate.h"

namespace blk {

enum class DeviceType : uint8_t {
    None = 0,
    Disk = 1,  // Hard disk
    Swap = 2,  // Swap device
};

// Hardware lifecycle, separate from registration and retained resource ownership.
enum class DeviceState : uint8_t {
    Offline,
    Initializing,
    Ready,
    Quarantined,
};

}  // namespace blk

struct BlockDevice {
    static constexpr size_t BLOCK_SIZE_BYTES = 512;

    blk::DeviceType type{};  // Device type
    uint32_t block_count{};  // Size in blocks
    char name[8]{};          // Device name

    // start_lba is the first logical block; block_count is the transfer length.
    Error read(uint32_t start_lba, void* buf, size_t block_count);
    Error write(uint32_t start_lba, const void* buf, size_t block_count);
    Error flush();
    virtual void print_info();

protected:
    virtual Error read_blocks(uint32_t start_lba, void* buf, size_t block_count) = 0;
    virtual Error write_blocks(uint32_t start_lba, const void* buf, size_t block_count) = 0;
    // Drivers must explicitly implement durable flush or a known cache-free completion.
    virtual Error flush_cache() { return Error::NotSupported; }
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

    static Error quiesce();
    static void finish_sync();
    static void resume();
    static Error flush_all();
    static bool is_frozen();


private:
    friend struct BlockDevice;
    static Error begin_io(ActivityGuard& guard);
    inline static ActivityGate io_gate_{};
    inline static bool flush_access_{};
    inline static sys::inplace_vector<BlockDevice*, MAX_DEVICES> devices_{};
};

namespace blk {

int init();

// Register architecture/platform-specific block backends
int probe_backends();
Error register_device(BlockDevice* device);

}  // namespace blk
