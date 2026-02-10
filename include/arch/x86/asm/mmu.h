#pragma once

#include "pg.h"
#include "seg.h"

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

#define ADDR_BITS 48  // canonical address length in bits (48-bit virtual)

#define PML4X_SHIFT 39  // offset of PML4 index in a linear address
#define PDPTX_SHIFT 30  // offset of PDPT index in a linear address
#define PDX_SHIFT   21  // offset of PD index in a linear address
#define PTX_SHIFT   12  // offset of PT index in a linear address

#define ENTRY_NUM   512  // entries per page table level (2^9)
#define ENTRY_MASK  (ENTRY_NUM - 1)  // 0x1FF

#define PG_MASK  (PG_SIZE - 1)  // page offset mask

#define PT_SIZE   (PG_SIZE * ENTRY_NUM)         // bytes mapped by one PT entry range = 2MB
#define PD_SIZE   ((uint64_t)PT_SIZE * ENTRY_NUM)  // bytes mapped by one PD entry range = 1GB
#define PDPT_SIZE ((uint64_t)PD_SIZE * ENTRY_NUM)  // bytes mapped by one PDPT entry = 512GB

// Page table level indices from a linear address
#define PML4X(la) ((((uintptr_t)(la)) >> PML4X_SHIFT) & ENTRY_MASK)
#define PDPTX(la) ((((uintptr_t)(la)) >> PDPTX_SHIFT) & ENTRY_MASK)
#define PDX(la)   ((((uintptr_t)(la)) >> PDX_SHIFT) & ENTRY_MASK)
#define PTX(la)   ((((uintptr_t)(la)) >> PTX_SHIFT) & ENTRY_MASK)

#define PTE_ADDR(pte) ((uintptr_t)(pte) & 0x000FFFFFFFFFF000ULL)

#define PDE_ADDR(pde) PTE_ADDR(pde)

#define PG_OFF(la) ((uintptr_t)(la) & PG_MASK)

// Compatibility aliases (PDE_NUM for old code)
#define PDE_NUM ENTRY_NUM
#define PTE_NUM ENTRY_NUM

#define P_ADDR(kva) ((uintptr_t)(kva) - KERNEL_BASE)          // convert kernel virtual address to physical address
#define K_ADDR(pa) ((void *)((uintptr_t)(pa) + KERNEL_BASE))  // convert physical address to kernel virtual address