#pragma once

// Memory that devices read and write directly.
//
// DmaBuffer owns whole, physically contiguous pages, so every buffer starts
// page aligned and never crosses a boundary the device cannot handle within
// one page. On machines whose devices bypass the CPU caches, the owner calls
// sync_for_device after the CPU writes and sync_for_cpu before it reads what
// a device wrote; elsewhere both cost nothing.

#include <base/types.h>
#include "lib/result.h"

struct Page;

class [[nodiscard]] DmaBuffer {
public:
    DmaBuffer() = default;
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;
    ~DmaBuffer();

    // Zero-filled and already synchronized for the device.
    static Result<DmaBuffer> alloc(size_t bytes);

    [[nodiscard]] void* data() const { return data_; }
    [[nodiscard]] uintptr_t pa() const { return pa_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] bool empty() const { return size_ == 0; }

    // Ranges are byte offsets into the buffer.
    void sync_for_device(size_t offset, size_t bytes) const;
    void sync_for_cpu(size_t offset, size_t bytes) const;

private:
    void release();

    Page* pages_{};
    size_t page_count_{};
    void* data_{};
    uintptr_t pa_{};
    size_t size_{};
};

namespace dma {

// Implemented by each architecture: writes back and invalidates the CPU
// cache lines covering [pa, pa + bytes) where devices are not coherent.
void writeback_invalidate(uintptr_t pa, size_t bytes);

}  // namespace dma
