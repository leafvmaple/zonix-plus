

#include <arch/x86/io.h>
#include <arch/x86/segments.h>
#include <arch/x86/mmu.h>

#include "math.h"
#include "stdio.h"
#include "../trap/trap.h"

#include "vmm.h"
#include "swap.h"

extern pde_t __boot_pgdir;
pde_t* boot_pgdir = &__boot_pgdir;

pte_t *const vpt = (pte_t *)VPT;
pde_t *const vpd = (pde_t *)PG_ADDR(PDX(VPT), PDX(VPT), 0);

MemoryDesc init_mm;

extern pde_t* boot_pgdir;

static const char* perm2str(int perm) {
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = (perm & PTE_P) ? 'r' : '-';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

static int get_pgtable_items(size_t start, size_t limit, uintptr_t *table, size_t *left, size_t *right) {
    if (start >= limit) {
        return 0;
    }
    while (start < limit && !(table[start] & PTE_P)) {
        start++;
    }
    if (start < limit) {
		*left = start;
        int perm = (table[start++] & PTE_USER);
        while (start < limit && (table[start] & PTE_USER) == perm) {
            start++;
        }
		*right = start;
        return perm;
    }
    return 0;
}

void print_pgdir() {
    cprintf("-------------------- BEGIN --------------------\n");
    size_t left, right = 0, perm;
    while ((perm = get_pgtable_items(right, PDE_NUM, vpd, &left, &right)) != 0) {
        cprintf("PDE(%03x) %08x-%08x %08x %s\n", right - left, left * PT_SIZE, right * PT_SIZE, (right - left) * PT_SIZE, perm2str(perm));
        size_t l, r = left * PTE_NUM;
        while ((perm = get_pgtable_items(r, right * PTE_NUM, vpt, &l, &r)) != 0) {
            cprintf("  |-- PTE(%05x) %08x-%08x %08x %s\n", r - l, l * PG_SIZE, r * PG_SIZE, (r - l) * PG_SIZE, perm2str(perm));
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

// fill all entries in page directory
static void pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
	size_t n = round_up(size, PG_SIZE) / PG_SIZE;
    la = round_down(la, PG_SIZE);
    pa = round_down(pa, PG_SIZE);
    for (; n > 0; n--, la += PG_SIZE, pa += PG_SIZE) {
        pte_t* ptep = get_pte(pgdir, la, 1);
        *ptep = pa | PTE_P | perm;
    }
}

void vmm_init() {
	boot_pgdir[PDX(VPT)] = P_ADDR(boot_pgdir) | PTE_P | PTE_W;
	cprintf("Page Director: [0x%x]\n", boot_pgdir);
    
	pgdir_init(boot_pgdir, KERNEL_BASE, KERNEL_MEM_SIZE, 0, PTE_W);

    mm_init(&init_mm);
    init_mm.pgdir = boot_pgdir;
}