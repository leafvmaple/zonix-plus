#pragma once

/**
 * @file page.h
 * @brief Architecture-neutral paging constants and types — AArch64.
 *
 * AArch64 implementation with 4KB granule, 4-level tables (48-bit VA).
 * Aggregates pg.h (raw PTE bits), memlayout.h (memory layout), and
 * mmu.h (address conversion + page table geometry).
 */

#include "pg.h"
#include "memlayout.h"

/* ===================================================================
 * Portable page permission flags  (VM_* — used by kernel/ code)
 * =================================================================== */

#define VM_PRESENT   (PTE_VALID | PTE_AF)
#define VM_WRITE     PTE_AP_RW_ALL
#define VM_USER      PTE_AP_RW_ALL   /* EL0 access            */
#define VM_NOCACHE   PTE_ATTR_DEVICE /* device/uncached MMIO  */
#define VM_LARGEPAGE PTE_BLOCK       /* 2MB block mapping     */
#define VM_NOEXEC    (PTE_UXN | PTE_PXN)

/* Convenience combo: user-accessible read/write */
#define VM_USER_RW (VM_USER | VM_PRESENT)

#ifndef __ASSEMBLY__

#include <base/types.h>

/* ===================================================================
 * Portable page table geometry
 * =================================================================== */

inline constexpr int PAGE_LEVELS = 4;
inline constexpr int PAGE_TABLE_ENTRIES = 512; /* 9-bit index per level */
inline constexpr int USER_TOP_ENTRIES = 256;   /* lower half of TTBR0   */

using pgd_t = uintptr_t;

/* ===================================================================
 * User-space address space layout (48-bit VA)
 * =================================================================== */

inline constexpr uintptr_t USER_SPACE_TOP = 0x0000800000000000ULL;
inline constexpr uintptr_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4 * PG_SIZE; /* 16 KB */

/* ===================================================================
 * Page table index extraction (4KB granule, 9-bit per level)
 * =================================================================== */

static inline int pml4x(uintptr_t va) {
    return (va >> 39) & 0x1FF;
} /* L0 */
static inline int pdptx(uintptr_t va) {
    return (va >> 30) & 0x1FF;
} /* L1 */
static inline int pdx(uintptr_t va) {
    return (va >> 21) & 0x1FF;
} /* L2 */
static inline int ptx(uintptr_t va) {
    return (va >> 12) & 0x1FF;
} /* L3 */

static inline uintptr_t pte_addr(uintptr_t pte) {
    return pte & 0x0000FFFFFFFFF000ULL;
}

/* Detect 2MB block entry at PMD level: valid=1, table=0 → bits[1:0]=0b01 */
static inline bool pte_is_block(uintptr_t entry) {
    return (entry & 3) == 1;
}

/*
 * AArch64 descriptor format helpers.
 *
 * Table descriptor (levels 0-2, points to next level):  PA | 0b11
 * Block descriptor (levels 1-2, 1GB/2MB mapping):       PA | attrs | 0b01
 * Page descriptor  (level 3, 4KB mapping):               PA | attrs | 0b11
 *
 * On x86, table entries and page entries share the same format (P + W + U).
 * On aarch64, they differ: table needs bit[1]=1, page also needs bit[1]=1,
 * but block has bit[1]=0.
 */

/* Create a table descriptor pointing to next-level page table */
static inline uintptr_t make_pte_table(uintptr_t pa) {
    return pa | PTE_VALID | PTE_TABLE;
}

/* Create a level-3 page descriptor (4KB page) */
static inline uintptr_t make_pte_page(uintptr_t pa, uint32_t perm) {
    return pa | PTE_VALID | PTE_PAGE | PTE_AF | perm;
}

#include "mmu.h"

#endif /* !__ASSEMBLY__ */
