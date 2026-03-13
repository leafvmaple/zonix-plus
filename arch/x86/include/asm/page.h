#pragma once

/**
 * @file page.h
 * @brief Architecture-neutral paging constants and types.
 *
 * Each architecture provides this header defining:
 *   - Page size, PTE flag names, page table geometry
 *   - Portable PTE flag aliases so kernel/ code never uses raw x86 names
 *   - User/kernel address space boundary constants
 *   - Page table walking helpers
 *
 * x86_64 implementation — delegates to pg.h and mmu.h.
 */

#include "pg.h"
#include "memlayout.h"

/* ===================================================================
 * Portable page permission flags
 *
 * kernel/ code should use these VM_* names exclusively.
 * Each arch maps them to native PTE bits.
 * =================================================================== */

#define VM_PRESENT   PTE_P               /* page is present / valid             */
#define VM_WRITE     PTE_W               /* page is writable                    */
#define VM_USER      PTE_U               /* page is accessible from user mode   */
#define VM_NOCACHE   (PTE_PCD | PTE_PWT) /* disable caching (MMIO) */
#define VM_LARGEPAGE PTE_PS              /* 2MB / section mapping               */
#define VM_NOEXEC    PTE_NX              /* no-execute                          */

/* Convenience combo: user-accessible read/write */
#define VM_USER_RW (VM_USER | VM_WRITE | VM_PRESENT)

#ifndef __ASSEMBLY__

#include "mmu.h"

/* ===================================================================
 * Portable page table geometry
 * =================================================================== */

/* Number of page table levels */
inline constexpr int PAGE_LEVELS = 4;

/* Number of entries per table (9-bit index) */
inline constexpr int PAGE_TABLE_ENTRIES = ENTRY_NUM; /* 512 */

/* User/kernel split: lower-half entries in top-level table */
inline constexpr int USER_TOP_ENTRIES = USER_PML4_ENTRIES; /* 256 */

/* Page table entry type (same as uintptr_t on both x86 and ARM) */
using pgd_t = uintptr_t; /* top-level page directory entry */

/* ===================================================================
 * User-space address space layout
 * =================================================================== */

inline constexpr uintptr_t USER_SPACE_TOP = 0x0000800000000000ULL;
inline constexpr uintptr_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4 * PG_SIZE; /* 16 KB */

#endif /* !__ASSEMBLY__ */
