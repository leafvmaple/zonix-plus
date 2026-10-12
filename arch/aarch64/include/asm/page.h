#pragma once

#define PTE_VALID (1UL << 0)
#define PTE_TABLE (1UL << 1)  /* table descriptor (levels 0-2) */
#define PTE_BLOCK (0UL << 1)  /* block descriptor (levels 1-2) */
#define PTE_PAGE  (1UL << 1)  /* page descriptor (level 3)     */
#define PTE_AF    (1UL << 10) /* access flag                   */

/* AttrIndex[2:0] in bits [4:2] */
#define PTE_ATTR_NORMAL (0UL << 2) /* use MAIR index 0 (normal)     */
#define PTE_ATTR_DEVICE (1UL << 2) /* use MAIR index 1 (device)     */

/* AP[2:1] in bits [7:6] */
#define PTE_AP_USER      (1UL << 6)
#define PTE_AP_READ_ONLY (1UL << 7)
#define PTE_AP_RW_EL1    (0UL << 6)       /* EL1 read/write, EL0 none      */
#define PTE_AP_RW_ALL    PTE_AP_USER      /* EL1+EL0 read/write          */
#define PTE_AP_RO_EL1    PTE_AP_READ_ONLY /* EL1 read-only, EL0 none */
#define PTE_AP_RO_ALL    (PTE_AP_USER | PTE_AP_READ_ONLY)

/* Hierarchical APTable restrictions apply to the entire subtree. */
#define PTE_AP_TABLE_NO_USER   (1UL << 61)
#define PTE_AP_TABLE_READ_ONLY (1UL << 62)
#define PTE_TYPE_MASK          (PTE_VALID | PTE_TABLE)
#define PTE_ADDR_MASK          0x0000FFFFFFFFF000UL

#define PTE_UXN (1UL << 54) /* unprivileged execute-never    */
#define PTE_PXN (1UL << 53) /* privileged execute-never      */

#define PG_SIZE  4096
#define PG_SHIFT 12
#define PG_MASK  (PG_SIZE - 1)

#include "memlayout.h"

#ifndef __ASSEMBLY__

#include <base/types.h>
#include <kernel/vm_flags.h>

using pte_t = uintptr_t;
using pde_t = uintptr_t;

inline constexpr int PAGE_LEVELS = 4;
inline constexpr int PT_WALK_LEVELS = 4;       /* actual hardware page table depth */
inline constexpr int PAGE_TABLE_ENTRIES = 512; /* 9-bit index per level */
inline constexpr int USER_TOP_ENTRIES = 256;   /* lower half of TTBR0   */

inline constexpr uintptr_t USER_SPACE_TOP = 0x0000800000000000ULL;
inline constexpr uintptr_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4 * PG_SIZE; /* 16 KB */

static inline int pml4_index(uintptr_t va) {
    return (va >> 39) & 0x1FF;
} /* L0 */
static inline int pdpt_index(uintptr_t va) {
    return (va >> 30) & 0x1FF;
} /* L1 */
static inline int pd_index(uintptr_t va) {
    return (va >> 21) & 0x1FF;
} /* L2 */
static inline int pt_index(uintptr_t va) {
    return (va >> 12) & 0x1FF;
} /* L3 */

static inline uintptr_t pte_addr(uintptr_t pte) {
    return pte & PTE_ADDR_MASK;
}

/* Detect 2MB block entry at PMD level: valid=1, table=0 → bits[1:0]=0b01 */
static inline bool pte_is_block(uintptr_t entry) {
    return (entry & PTE_TYPE_MASK) == PTE_VALID;
}

static inline uintptr_t make_pte_table(uintptr_t pa) {
    return pa | PTE_VALID | PTE_TABLE;
}

static inline uintptr_t make_pte_page(uintptr_t pa, uint32_t perm) {
    uintptr_t flags = PTE_VALID | PTE_PAGE | PTE_AF;
    if ((perm & VM_WRITE) == 0) {
        flags |= PTE_AP_READ_ONLY;
    }
    if ((perm & VM_USER) != 0) {
        flags |= PTE_AP_USER | PTE_PXN;
    }
    if ((perm & VM_NOEXEC) != 0) {
        flags |= PTE_UXN | PTE_PXN;
    }
    if ((perm & VM_NOCACHE) != 0) {
        flags |= PTE_ATTR_DEVICE;
    }
    return pa | flags;
}

static inline uintptr_t split_page_entry(uintptr_t entry, uintptr_t pa, int child_shift) {
    uintptr_t flags = entry & ~(PTE_ADDR_MASK | PTE_TYPE_MASK);
    return pa | flags | PTE_VALID | (child_shift == PG_SHIFT ? PTE_PAGE : PTE_BLOCK);
}

static inline bool pte_present(uintptr_t entry) {
    return (entry & PTE_VALID) != 0;
}
static inline bool pte_writable(uintptr_t entry) {
    return (entry & PTE_AP_READ_ONLY) == 0;
}
static inline bool pte_user(uintptr_t entry) {
    return (entry & PTE_AP_USER) != 0;
}
static inline bool pte_user_accessible(uintptr_t entry, bool write) {
    return pte_present(entry) && pte_user(entry) && (!write || pte_writable(entry));
}
static inline bool pte_table_user(uintptr_t entry) {
    return (entry & PTE_AP_TABLE_NO_USER) == 0;
}
static inline bool pte_table_writable(uintptr_t entry) {
    return (entry & PTE_AP_TABLE_READ_ONLY) == 0;
}
static inline bool pte_user_table_accessible(uintptr_t entry, bool write) {
    return pte_table_user(entry) && (!write || pte_table_writable(entry));
}
static inline bool pte_executable(uintptr_t entry) {
    return (entry & PTE_UXN) == 0;
}
static inline uint32_t merge_user_page_perm(uintptr_t entry, uint32_t perm) {
    return user_page_perm(pte_writable(entry) || (perm & VM_WRITE) != 0,
                          pte_executable(entry) || (perm & VM_NOEXEC) == 0);
}

#endif /* !__ASSEMBLY__ */
