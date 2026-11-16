#pragma once

#include "lib/result.h"
#include <debug/assert.h>
#include <sys/intrusive_list.hpp>
#include <sys/memory.hpp>

namespace vmm {

class MmioAllocator;

class MmioMapping {
public:
    MmioMapping(const MmioMapping&) = delete;
    MmioMapping& operator=(const MmioMapping&) = delete;
    MmioMapping(MmioMapping&&) = delete;
    MmioMapping& operator=(MmioMapping&&) = delete;
    uintptr_t address() const { return va_ + offset_; }
    size_t size() const { return size_; }

private:
    friend class MmioAllocator;
    MmioMapping() = default;
    uintptr_t va_{};
    size_t span_{};
    size_t offset_{};
    size_t size_{};
    sys::list_node<MmioMapping> node_{*this};
};

struct MmioUnmap {
    void operator()(MmioMapping* mapping) const;
};

using MmioRegion = sys::unique_ptr<MmioMapping, MmioUnmap>;

// Physical device memory is borrowed; the owner releases only the VA mapping.
Result<MmioRegion> map_mmio(uintptr_t pa, size_t byte_count, uint32_t perm);

}  // namespace vmm
