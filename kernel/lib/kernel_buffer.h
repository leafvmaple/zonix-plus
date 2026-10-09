#pragma once

#include "lib/memory.h"
#include "lib/result.h"
#include <sys/memory.hpp>

namespace buffer_detail {
struct Free {
    void operator()(uint8_t* ptr) const noexcept { kfree(ptr); }
};
}  // namespace buffer_detail

// Raw bytes allocated by kmalloc: no element construction or destruction.
class [[nodiscard]] KernelBuffer {
public:
    constexpr KernelBuffer() noexcept = default;
    KernelBuffer(const KernelBuffer&) = delete;
    KernelBuffer& operator=(const KernelBuffer&) = delete;
    KernelBuffer(KernelBuffer&& other) noexcept : data_(sys::move(other.data_)), size_(other.size_) { other.size_ = 0; }
    KernelBuffer& operator=(KernelBuffer&& other) noexcept {
        if (this != &other) {
            data_ = sys::move(other.data_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] static Result<KernelBuffer> alloc(size_t size) {
        if (size == 0) {
            return KernelBuffer{};
        }
        auto* data = static_cast<uint8_t*>(kmalloc(size));
        ENSURE(data, Error::NoMem);
        return KernelBuffer(data, size);
    }
    [[nodiscard]] uint8_t* data() & noexcept { return data_.get(); }
    [[nodiscard]] const uint8_t* data() const& noexcept { return data_.get(); }
    uint8_t* data() && = delete;
    const uint8_t* data() const&& = delete;
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

private:
    KernelBuffer(uint8_t* data, size_t size) noexcept : data_(data), size_(size) {}
    sys::unique_ptr<uint8_t, buffer_detail::Free> data_;
    size_t size_{};
};
