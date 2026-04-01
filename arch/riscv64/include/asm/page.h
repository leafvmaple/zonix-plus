#pragma once

/*
 * RISC-V Sv39 page table definitions (4KB granule, 3-level PT)
 *
 * Virtual address breakdown (39-bit, sign-extended):
 *   [63:39] sign extension of bit 38
 *   [38:30] VPN[2]  — root page table index  (9 bits)
 *   [29:21] VPN[1]  — second level index      (9 bits)
 *   [20:12] VPN[0]  — third level (leaf) index (9 bits)
 *   [11:0]  page offset                        (12 bits)
 *
 * PTE format (64-bit):
 *   [63:54] reserved
 *   [53:10] PPN[2:0] — physical page number (44 bits)
 *   [9:8]   RSW      — reserved for OS
 *   [7]     D        — dirty
 *   [6]     A        — accessed
 *   [5]     G        — global
 *   [4]     U        — user-accessible
 *   [3]     X        — executable
 *   [2]     W        — writable
 *   [1]     R        — readable
 *   [0]     V        — valid
 */

#include "memlayout.h"

/* PTE permission bits */
#define PTE_V (1UL << 0) /* valid */
#define PTE_R (1UL << 1) /* readable */
#define PTE_W (1UL << 2) /* writable */
#define PTE_X (1UL << 3) /* executable */
#define PTE_U (1UL << 4) /* user-accessible */
#define PTE_G (1UL << 5) /* global */
#define PTE_A (1UL << 6) /* accessed */
#define PTE_D (1UL << 7) /* dirty */

/* A PTE with R=W=X=0 is a pointer to the next level (non-leaf).
 * A leaf PTE has at least one of R/W/X set. */
#define PTE_TABLE PTE_V /* non-leaf: valid, R=W=X=0 */

/* Common permission combos */
#define PTE_KERN_RW  (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)
#define PTE_KERN_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D)
#define PTE_USER_RW  (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)

/* VM_* generic permission flags used by kernel/mm/vmm.cpp */
#define VM_PRESENT   (PTE_V | PTE_A)
#define VM_WRITE     PTE_W
#define VM_USER      PTE_U
#define VM_NOCACHE   0UL /* no separate uncached bit in Sv39 */
#define VM_LARGEPAGE 0UL /* 2MB: VPN[0]=0, leaf at level 2  */
#define VM_NOEXEC    0UL /* absence of PTE_X = no-execute   */

#define VM_USER_RW (VM_PRESENT | VM_WRITE | VM_USER)

/* Page size */
#define PG_SIZE  4096
#define PG_SHIFT 12
#define PG_MASK  (PG_SIZE - 1)

/* Physical address from PTE */
#define PTE_PPN_SHIFT 10
#define PTE_ADDR(pte) (((pte) >> PTE_PPN_SHIFT) << PG_SHIFT)

/* satp register: MODE=8 (Sv39), ASID=0, PPN=root page table */
#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgdir_pa) (SATP_SV39 | ((pgdir_pa) >> PG_SHIFT))

#ifndef __ASSEMBLY__

#include <base/types.h>

inline constexpr int PAGE_LEVELS = 4;          /* swap.cpp walks 4 named levels; see below */
inline constexpr int PT_WALK_LEVELS = 3;       /* actual hardware page table depth (Sv39)  */
inline constexpr int PAGE_TABLE_ENTRIES = 512; /* 9-bit index per level */
inline constexpr int USER_TOP_ENTRIES = 256;   /* VPN[2] 0..255 = user space */

/*
 * Generic level-shift constants expected by kernel/mm/swap.cpp.
 *
 * Sv39 only has 3 true levels (PGD/PMD/PT).  swap.cpp hardcodes a
 * 4-element {PML4X, PDPTX, PDX, PTX} array sized by PAGE_LEVELS.
 * To keep the generic code compiling we declare PAGE_LEVELS = 4 and
 * alias PTX_SHIFT = PDX_SHIFT (the phantom 4th level is never reached
 * in practice because Sv39 leaf entries are detected by R/W/X bits
 * before depth 3 is entered).
 *
 * PT_WALK_LEVELS = 3 is the actual hardware depth used by pmm.cpp's
 * page table walker.  The ordering matches LEVEL_SHIFTS[]:
 *   LEVEL_SHIFTS[0] = PML4X_SHIFT = 30  (PGD)
 *   LEVEL_SHIFTS[1] = PDPTX_SHIFT = 21  (PMD)
 *   LEVEL_SHIFTS[2] = PDX_SHIFT   = 12  (PT)
 */
inline constexpr int PML4X_SHIFT = 30; /* Sv39 L0 (PGD, 1GB gigapages)  */
inline constexpr int PDPTX_SHIFT = 21; /* Sv39 L1 (PMD, 2MB megapages)  */
inline constexpr int PDX_SHIFT = 12;   /* Sv39 L2 (PT,  4KB pages)      */
inline constexpr int PTX_SHIFT = 12;   /* phantom L3 — same as PDX      */

inline constexpr uintptr_t USER_SPACE_TOP = 0x0000003FFFFFFFFFULL;
inline constexpr uintptr_t USER_STACK_TOP = 0x0000003FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4 * PG_SIZE;

static inline int pml4_index(uintptr_t va) {
    return (va >> 30) & 0x1FF;
} /* VPN[2] */
static inline int pdpt_index(uintptr_t va) {
    return (va >> 21) & 0x1FF;
} /* VPN[1] */
static inline int pd_index(uintptr_t va) {
    return (va >> 12) & 0x1FF;
} /* VPN[0] */
static inline int pt_index(uintptr_t va) {
    return (va >> 12) & 0x1FF;
} /* alias   */

static inline uintptr_t pte_addr(uintptr_t pte) {
    return PTE_ADDR(pte);
}

/* True if the PTE is a leaf (R, W or X is set) */
static inline bool pte_is_leaf(uintptr_t pte) {
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

/* Leaf at the PMD level (VPN[1]): 2MB megapage */
static inline bool pte_is_block(uintptr_t pte) {
    return pte_is_leaf(pte); /* same check; caller ensures level == 1 */
}

static inline uintptr_t make_pte_table(uintptr_t pa) {
    return ((pa >> PG_SHIFT) << PTE_PPN_SHIFT) | PTE_V;
}

static inline uintptr_t make_pte_page(uintptr_t pa, uint32_t perm) {
    return ((pa >> PG_SHIFT) << PTE_PPN_SHIFT) | PTE_V | PTE_R | PTE_A | PTE_D | perm;
}

/* pde_addr: extract PA from a non-leaf PTE (same as pte_addr) */
inline constexpr uintptr_t pde_addr(uintptr_t pde) {
    return PTE_ADDR(pde);
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
        if (chunk > size)
            chunk = size;
        func(va, chunk);
        va += chunk;
        size -= chunk;
    }
}

#endif /* !__ASSEMBLY__ */
