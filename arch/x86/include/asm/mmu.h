#pragma once

#include "pg.h"
#include "memlayout.h"

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

// Page table constants
inline constexpr int ADDR_BITS = 48;

inline constexpr int PML4X_SHIFT = 39;
inline constexpr int PDPTX_SHIFT = 30;
inline constexpr int PDX_SHIFT = 21;
inline constexpr int PTX_SHIFT = 12;

inline constexpr int ENTRY_NUM = 512;
inline constexpr int ENTRY_MASK = ENTRY_NUM - 1;  // 0x1FF

// PML4 entries 0..255 cover user space, 256..511 cover kernel space
inline constexpr int USER_PML4_ENTRIES = ENTRY_NUM / 2;

inline constexpr uintptr_t PG_MASK = PG_SIZE - 1;

inline constexpr uint64_t PT_SIZE = (uint64_t)PG_SIZE * ENTRY_NUM;  // 2MB
inline constexpr uint64_t PD_SIZE = PT_SIZE * ENTRY_NUM;            // 1GB
inline constexpr uint64_t PDPT_SIZE = PD_SIZE * ENTRY_NUM;          // 512GB

// Compatibility aliases
inline constexpr int PDE_NUM = ENTRY_NUM;
inline constexpr int PTE_NUM = ENTRY_NUM;

// Page table level indices from a linear address
inline constexpr uintptr_t pml4x(uintptr_t la) {
    return (la >> PML4X_SHIFT) & ENTRY_MASK;
}
inline constexpr uintptr_t pdptx(uintptr_t la) {
    return (la >> PDPTX_SHIFT) & ENTRY_MASK;
}
inline constexpr uintptr_t pdx(uintptr_t la) {
    return (la >> PDX_SHIFT) & ENTRY_MASK;
}
inline constexpr uintptr_t ptx(uintptr_t la) {
    return (la >> PTX_SHIFT) & ENTRY_MASK;
}

// Extract physical page address from page table entry
inline constexpr uintptr_t pte_addr(uintptr_t pte) {
    return pte & 0x000FFFFFFFFFF000ULL;
}
inline constexpr uintptr_t pde_addr(uintptr_t pde) {
    return pte_addr(pde);
}

// Page offset from a linear address
inline constexpr uintptr_t pg_off(uintptr_t la) {
    return la & PG_MASK;
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