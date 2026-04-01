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
#define PTE_AP_RW_EL1 (0UL << 6) /* EL1 read/write, EL0 none      */
#define PTE_AP_RW_ALL (1UL << 6) /* EL1+EL0 read/write            */
#define PTE_AP_RO_EL1 (2UL << 6) /* EL1 read-only, EL0 none       */
#define PTE_AP_RO_ALL (3UL << 6) /* EL1+EL0 read-only             */

#define PTE_UXN (1UL << 54) /* unprivileged execute-never    */
#define PTE_PXN (1UL << 53) /* privileged execute-never      */

#define PG_SIZE  4096
#define PG_SHIFT 12
#define PG_MASK  (PG_SIZE - 1)

#include "memlayout.h"

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
    return pte & 0x0000FFFFFFFFF000ULL;
}

/* Detect 2MB block entry at PMD level: valid=1, table=0 → bits[1:0]=0b01 */
static inline bool pte_is_block(uintptr_t entry) {
    return (entry & 3) == 1;
}

static inline uintptr_t make_pte_table(uintptr_t pa) {
    return pa | PTE_VALID | PTE_TABLE;
}

static inline uintptr_t make_pte_page(uintptr_t pa, uint32_t perm) {
    return pa | PTE_VALID | PTE_PAGE | PTE_AF | perm;
}

#endif /* !__ASSEMBLY__ */
