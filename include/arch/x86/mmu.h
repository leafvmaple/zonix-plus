#pragma once

#include "asm/pg.h"
#include "asm/seg.h"

#define ADDR_BITS 32  // address length in bits

#define PTX_SHIFT 12  // offset of PTX in a linear address
#define PDX_SHIFT 22  // offset of PDX in a linear address

#define PDE_NUM     (1 << (ADDR_BITS - PDX_SHIFT))  // number of entries in a page directory
#define PTE_NUM     (1 << (PDX_SHIFT - PTX_SHIFT))  // number of entries in a

#define PDX_MASK (PDE_NUM - 1)  // mask for PDX
#define PTX_MASK (PTE_NUM - 1)  // mask for PTX
#define PG_MASK  (PG_SIZE - 1)  // size of a page in bytes

#define PT_SIZE  (PG_SIZE * PTE_NUM)  // bytes mapped by a page directory entry

// page directory index
#define PDX(la) ((((uintptr_t)(la)) >> PDX_SHIFT) & PDX_MASK)

// page table index
#define PTX(la) ((((uintptr_t)(la)) >> PTX_SHIFT) & PTX_MASK)

#define PTE_ADDR(pte) ((uintptr_t)(pte) & ~0xFFF)

#define PDE_ADDR(pde) PTE_ADDR(pde)

#define PG_OFF(la) ((uintptr_t)(la) & PG_MASK)

#define PG_ADDR(d, t, o) ((uintptr_t)((d) << PDX_SHIFT | (t) << PTX_SHIFT | (o)))

#define P_ADDR(kva) ((uintptr_t)(kva) - KERNEL_BASE)          // convert kernel virtual address to physical address
#define K_ADDR(pa) ((void *)((uintptr_t)(pa) + KERNEL_BASE))  // convert physical address to kernel virtual address