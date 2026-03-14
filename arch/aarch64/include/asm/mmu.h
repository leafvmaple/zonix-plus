#pragma once

#include "pg.h"
#include "memlayout.h"

#ifndef __ASSEMBLY__

/*
 * AArch64 4-level page tables (4KB granule, 48-bit VA):
 *   PGD (Level 0)  -> 512 entries, each covers 512GB
 *   PUD (Level 1)  -> 512 entries, each covers 1GB
 *   PMD (Level 2)  -> 512 entries, each covers 2MB
 *   PTE (Level 3)  -> 512 entries, each covers 4KB
 *
 * Virtual address breakdown (48-bit, TTBR1 high half):
 *   [63:48] all 1s (TTBR1 selection)
 *   [47:39] PGD index  (9 bits)
 *   [38:30] PUD index  (9 bits)
 *   [29:21] PMD index  (9 bits)
 *   [20:12] PTE index  (9 bits)
 *   [11:0]  Page offset (12 bits)
 */

// Page table constants
inline constexpr int ADDR_BITS = 48;

inline constexpr int PML4X_SHIFT = 39;
inline constexpr int PDPTX_SHIFT = 30;
inline constexpr int PDX_SHIFT = 21;
inline constexpr int PTX_SHIFT = 12;

inline constexpr int ENTRY_NUM = 512;
inline constexpr int ENTRY_MASK = ENTRY_NUM - 1;  // 0x1FF

// PGD entries 0..255 cover user space (TTBR0), 256..511 kernel (TTBR1)
inline constexpr int USER_PML4_ENTRIES = ENTRY_NUM / 2;

// Extract physical page address from page table entry
inline constexpr uintptr_t pde_addr(uintptr_t pde) {
    return pde & 0x0000FFFFFFFFF000ULL;
}

// Page offset from a linear address
inline constexpr uintptr_t pg_off(uintptr_t la) {
    return la & (PG_SIZE - 1);
}

// Convert kernel virtual address to physical address
inline constexpr uintptr_t virt_to_phys(uintptr_t kva) {
    return kva - KERNEL_BASE;
}

template<typename T>
inline uintptr_t virt_to_phys(T* kva) {
    return reinterpret_cast<uintptr_t>(kva) - KERNEL_BASE;
}

// Convert physical address to kernel virtual address
template<typename T = void>
inline T* phys_to_virt(uintptr_t pa) {
    return reinterpret_cast<T*>(pa + KERNEL_BASE);
}

// Iterate over page-aligned chunks within [va, va+size)
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
