#pragma once

#include "memlayout.h"

#define PTE_P   0x001         // Present
#define PTE_W   0x002         // Writeable
#define PTE_U   0x004         // User
#define PTE_PWT 0x008         // Write-Through (disable write-back cache for this page)
#define PTE_PCD 0x010         // Cache Disable (disable caching for this page, important for MMIO)
#define PTE_PS  0x080         // Page Size (2MB page when set in PDE)
#define PTE_NX  (1ULL << 63)  // No-Execute (requires EFER.NXE)

#define PTE_USER (PTE_U | PTE_W | PTE_P)

#define PG_SIZE  4096
#define PG_SHIFT 12  // 2^12 = 4096

#define VM_PRESENT   PTE_P               /* page is present / valid             */
#define VM_WRITE     PTE_W               /* page is writable                    */
#define VM_USER      PTE_U               /* page is accessible from user mode   */
#define VM_NOCACHE   (PTE_PCD | PTE_PWT) /* disable caching (MMIO) */
#define VM_LARGEPAGE PTE_PS              /* 2MB / section mapping               */

#define VM_NOEXEC PTE_NX /* no-execute                          */

#define VM_USER_RW (VM_USER | VM_WRITE | VM_PRESENT)

#ifndef __ASSEMBLY__

#include <base/types.h>

static inline bool pte_is_block(uintptr_t entry) {
    return (entry & PTE_PS) != 0;
}

static inline uintptr_t make_pte_table(uintptr_t pa) {
    return pa | VM_USER_RW;
}

static inline uintptr_t make_pte_page(uintptr_t pa, uint32_t perm) {
    return pa | VM_PRESENT | perm;
}

inline constexpr int PAGE_LEVELS = 4;
inline constexpr int PAGE_TABLE_ENTRIES = 512;
inline constexpr int USER_TOP_ENTRIES = 256;

inline constexpr uintptr_t USER_SPACE_TOP = 0x0000800000000000ULL;
inline constexpr uintptr_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4ULL * PG_SIZE; /* 16 KB */

#include "mmu.h"

#endif /* !__ASSEMBLY__ */
