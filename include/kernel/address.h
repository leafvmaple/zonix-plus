#pragma once

#include <base/types.h>
#include <asm/memlayout.h>
#include <asm/page.h>

// Conversions for the kernel's direct mapping, where a physical address and
// its kernel virtual address differ by KERNEL_BASE on every architecture.

inline constexpr uintptr_t virt_to_phys(uintptr_t kva) {
    return kva - KERNEL_BASE;
}

template<typename T>
inline uintptr_t virt_to_phys(T* kva) {
    return reinterpret_cast<uintptr_t>(kva) - KERNEL_BASE;
}

template<typename T = void>
inline T* phys_to_virt(uintptr_t pa) {
    return reinterpret_cast<T*>(pa + KERNEL_BASE);
}

// Calls func(address, length) for each piece of the range that lies within one page.
template<typename Callback>
inline void iterate_pages(uintptr_t va, size_t size, Callback&& func) {
    while (size > 0) {
        size_t offset = va & PG_MASK;
        size_t chunk = PG_SIZE - offset;
        if (chunk > size) {
            chunk = size;
        }

        func(va, chunk);

        va += chunk;
        size -= chunk;
    }
}
