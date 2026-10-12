#pragma once

#include "lib/kernel_buffer.h"

namespace virtio {

struct Completion {
    uint32_t id;
    uint32_t length;
};

// Owns split-ring storage. The device must be reset before release/destruction.
// Buffer ownership and interpretation belong to the device driver.
class SplitQueue {
public:
    SplitQueue() = default;
    SplitQueue(const SplitQueue&) = delete;
    SplitQueue& operator=(const SplitQueue&) = delete;
    Error allocate(uint16_t size);
    Error set_buffer(uint16_t id, uintptr_t pa, uint32_t length, bool device_writes);
    Error submit(uint16_t id);
    Result<Completion> pop_used();
    void release();
    uint16_t size() const { return size_; }
    uintptr_t descriptor_pa() const;
    uintptr_t available_pa() const;
    uintptr_t used_pa() const;

private:
    struct Descriptor {
        uint64_t address;
        uint32_t length;
        uint16_t flags;
        uint16_t next;
    };
    struct Available {
        uint16_t flags;
        uint16_t index;
        uint16_t ring[];
    };
    struct Used {
        uint16_t flags;
        uint16_t index;
        Completion ring[];
    };
    static_assert(sizeof(Descriptor) == 16 && sizeof(Completion) == 8);
    KernelBuffer descriptors_{};
    KernelBuffer available_{};
    KernelBuffer used_{};
    uint16_t size_{};
    uint16_t last_used_{};
};

}  // namespace virtio
