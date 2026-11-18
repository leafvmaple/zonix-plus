#pragma once

#include "lib/kernel_buffer.h"

namespace virtio {

struct Completion {
    uint32_t id;
    uint32_t length;
};

// Owns split-ring storage. Each of the three areas must be physically
// contiguous and its DMA base must meet the alignment below. The areas do not
// need page alignment or to be adjacent.
// The device must be reset before release/destruction. Buffer ownership and
// interpretation belong to the device driver.
class SplitQueue {
public:
    static constexpr size_t DESCRIPTOR_ALIGNMENT = 16;
    static constexpr size_t AVAILABLE_ALIGNMENT = 2;
    static constexpr size_t USED_ALIGNMENT = 4;

    SplitQueue() = default;
    SplitQueue(const SplitQueue&) = delete;
    SplitQueue& operator=(const SplitQueue&) = delete;
    Error allocate(uint16_t size);
    void suppress_interrupts();
    Error set_buffer(uint16_t id, uintptr_t pa, uint32_t length, bool device_writes);
    Error submit(uint16_t id);
    Result<Completion> pop_used();
    bool has_used() const;
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
