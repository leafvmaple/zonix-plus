#pragma once

#include "page.h"
#include "memlayout.h"

#ifndef __ASSEMBLY__

/*
 * x86_64 4-level paging:
 *   PML4 (Page Map Level 4)  -> 512 entries, each covers 512GB
 *   PDPT (Page Directory Pointer Table) -> 512 entries, each covers 1GB
 *   PD   (Page Directory)    -> 512 entries, each covers 2MB
 *   PT   (Page Table)        -> 512 entries, each covers 4KB
 *
 * Virtual address breakdown (48-bit canonical):
 *   [63:48] sign extension of bit 47
 *   [47:39] PML4 index  (9 bits)
 *   [38:30] PDPT index  (9 bits)
 *   [29:21] PD index    (9 bits)
 *   [20:12] PT index    (9 bits)
 *   [11:0]  Page offset (12 bits)
 */

inline constexpr uintptr_t pte_addr(uintptr_t pte) {
    return pte & 0x000FFFFFFFFFF000ULL;
}
inline constexpr uintptr_t pde_addr(uintptr_t pde) {
    return pte_addr(pde);
}

inline constexpr uintptr_t pg_off(uintptr_t la) {
    return la & PG_MASK;
}

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

template<typename TFunc>
inline void iterate_pages(uintptr_t va, size_t size, TFunc&& func) {
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

#endif /* !__ASSEMBLY__ */