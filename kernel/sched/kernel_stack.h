#pragma once

#include <base/types.h>
#include <asm/page.h>

#include "lib/result.h"

// A task's kernel stack with a guard page below it.
//
// The guard is the lowest page of the allocation, removed from the kernel
// mapping while the stack is in use. Running off the bottom of the stack then
// faults instead of writing into whatever memory happens to lie below. A
// single frame larger than a page can still step over the guard.
class [[nodiscard]] KernelStack {
public:
    static constexpr size_t SIZE = 16384;

    constexpr KernelStack() noexcept = default;
    KernelStack(const KernelStack&) = delete;
    KernelStack& operator=(const KernelStack&) = delete;
    KernelStack(KernelStack&& other) noexcept : block_(other.block_) { other.block_ = 0; }
    KernelStack& operator=(KernelStack&& other) noexcept {
        if (this != &other) {
            release();
            block_ = other.block_;
            other.block_ = 0;
        }
        return *this;
    }
    ~KernelStack() { release(); }

    [[nodiscard]] static Result<KernelStack> alloc();

    [[nodiscard]] bool empty() const { return block_ == 0; }
    // The lowest usable address; the stack grows down towards it from base() + SIZE.
    [[nodiscard]] uintptr_t base() const { return block_ == 0 ? 0 : block_ + PG_SIZE; }
    [[nodiscard]] bool guards(uintptr_t address) const {
        return block_ != 0 && address >= block_ && address < block_ + PG_SIZE;
    }

private:
    explicit KernelStack(uintptr_t block) noexcept : block_(block) {}
    void release();

    // Start of the allocation, which is the guard page.
    uintptr_t block_{};
};
