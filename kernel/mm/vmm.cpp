

#include <asm/arch.h>
#include <asm/page.h>

#include "lib/math.h"
#include "lib/stdio.h"
#include "trap/trap.h"

#include "vmm.h"
#include "swap.h"

#if defined(__aarch64__)
extern pde_t __boot_pgd_high;
pde_t* boot_pgdir = &__boot_pgd_high;
#else
extern pde_t __boot_pml4;
pde_t* boot_pgdir = &__boot_pml4;
#endif

MemoryDesc init_mm;

static const char* perm2str(int perm) {
    static char str[4];
    str[0] = (perm & VM_USER) ? 'u' : '-';
    str[1] = (perm & VM_PRESENT) ? 'r' : '-';
    str[2] = (perm & VM_WRITE) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

static void mm_init(MemoryDesc* mm) {
    mm->pgdir = nullptr;
    mm->map_count = 0;
}

namespace vmm {

void print_pgdir() {
    cprintf("-------------------- BEGIN --------------------\n");
    cprintf("PML4 at %p\n", boot_pgdir);
    // Simple dump of PML4 entries that are present
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        if (boot_pgdir[i] & VM_PRESENT) {
            cprintf("  PML4[%03d] = 0x%016lx %s\n", i, boot_pgdir[i], perm2str(boot_pgdir[i] & VM_USER_RW));
        }
    }
    cprintf("--------------------- END ---------------------\n");
}

int pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t addr) {
    uint32_t perm = VM_USER;
    Page* page = nullptr;

    addr = round_down(addr, PG_SIZE);

    pte_t* ptep = pmm::get_pte(mm->pgdir, addr, 1);
    if (*ptep == 0) {
        page = pmm::pgdir_alloc_page(mm->pgdir, addr, perm);
    } else {
        swap::in(mm, addr, &page);
    }

    return 0;
}

// Map virtual pages to physical pages in 4-level page table
int pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
    size_t n = round_up(size, PG_SIZE) / PG_SIZE;
    la = round_down(la, PG_SIZE);
    pa = round_down(pa, PG_SIZE);
    for (; n > 0; n--, la += PG_SIZE, pa += PG_SIZE) {
        pte_t* ptep = pmm::get_pte(pgdir, la, 1);
        if (!ptep) {
            cprintf("vmm: pgdir_init failed to allocate PTE for va=0x%lx\n", la);
            return -1;
        }
        *ptep = make_pte_page(pa, perm);
    }
    return 0;
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
    if (pgdir_init(boot_pgdir, va, size, phys_addr, perm) != 0) {
        cprintf("vmm: mmio_map failed for phys=0x%lx size=0x%lx\n", phys_addr, size);
        return 0;
    }
    // Flush TLB for the newly mapped range so that stale entries
    // (e.g. from split 2MB blocks) don't interfere.
    arch_flush_tlb_range(va, size);
    mmio_next_va += size;
    return va;
}

int init() {
    if (!boot_pgdir) {
        cprintf("vmm: boot page directory is null\n");
        return -1;
    }

    cprintf("PML4 (Page Map Level 4): [0x%p]\n", boot_pgdir);

    if (pgdir_init(boot_pgdir, KERNEL_BASE, KERNEL_MEM_SIZE, 0, VM_WRITE) != 0) {
        cprintf("vmm: failed to map kernel address space\n");
        return -1;
    }

    arch_flush_tlb_range(KERNEL_BASE, KERNEL_MEM_SIZE);

    mm_init(&init_mm);
    init_mm.pgdir = boot_pgdir;

    cprintf("vmm: kernel mapped [0x%lx, 0x%lx)\n", static_cast<uint64_t>(KERNEL_BASE),
            static_cast<uint64_t>(KERNEL_BASE + KERNEL_MEM_SIZE));
    return 0;
}

}  // namespace vmm