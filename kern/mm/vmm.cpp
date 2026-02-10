

#include <arch/x86/io.h>
#include <arch/x86/segments.h>
#include <arch/x86/mmu.h>

#include "math.h"
#include "stdio.h"
#include "../trap/trap.h"

#include "vmm.h"
#include "swap.h"

extern pde_t __boot_pml4;
pde_t* boot_pgdir = &__boot_pml4;  // PML4 is the top-level "page directory" in x86_64

MemoryDesc init_mm;

static const char* perm2str(int perm) {
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = (perm & PTE_P) ? 'r' : '-';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

void print_pgdir() {
    cprintf("-------------------- BEGIN --------------------\n");
    cprintf("PML4 at %p\n", boot_pgdir);
    // Simple dump of PML4 entries that are present
    for (int i = 0; i < 512; i++) {
        if (boot_pgdir[i] & PTE_P) {
            cprintf("  PML4[%03d] = 0x%016lx %s\n", i, boot_pgdir[i], perm2str(boot_pgdir[i] & PTE_USER));
        }
    }
    cprintf("--------------------- END ---------------------\n");
}

int vmm_pg_fault(MemoryDesc *mm, uint32_t error_code, uintptr_t addr) {
    uint32_t perm = PTE_U;
    Page *page = nullptr;

    addr = round_down(addr, PG_SIZE);

    pte_t *ptep = get_pte(mm->pgdir, addr, 1);
    if (*ptep == 0) {
        page = pgdir_alloc_page(mm->pgdir, addr, perm);
    } else {
        swap_in(mm, addr, &page);
    }

    return 0;
}

static void mm_init(MemoryDesc *mm) {
    mm->mmap_list.init();
    mm->pgdir = nullptr;
    mm->map_count = 0;
}

// Map virtual pages to physical pages in 4-level page table
void pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
	size_t n = round_up(size, PG_SIZE) / PG_SIZE;
    la = round_down(la, PG_SIZE);
    pa = round_down(pa, PG_SIZE);
    for (; n > 0; n--, la += PG_SIZE, pa += PG_SIZE) {
        pte_t* ptep = get_pte(pgdir, la, 1);
        *ptep = pa | PTE_P | perm;
    }
}

// -------------------------------------------------------------------------
// MMIO virtual address allocator
// Assigns consecutive virtual addresses starting at KERNEL_DEVIO_BASE.
// The virtual address has NO arithmetic relationship to the physical one.
// -------------------------------------------------------------------------
static uintptr_t mmio_next_va = KERNEL_DEVIO_BASE;

uintptr_t mmio_map(uintptr_t phys_addr, size_t size, uint32_t perm) {
    size = round_up(size, PG_SIZE);
    uintptr_t va = mmio_next_va;
    pgdir_init(boot_pgdir, va, size, phys_addr, perm);
    mmio_next_va += size;
    return va;
}

void vmm_init() {
	cprintf("PML4 (Page Map Level 4): [0x%p]\n", boot_pgdir);
    
	pgdir_init(boot_pgdir, KERNEL_BASE, KERNEL_MEM_SIZE, 0, PTE_W);

    mm_init(&init_mm);
    init_mm.pgdir = boot_pgdir;
}