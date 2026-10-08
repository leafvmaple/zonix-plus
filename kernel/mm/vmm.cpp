#include <asm/arch.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "lib/math.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "drivers/intr.h"
#include "trap/trap.h"

#include "vmm.h"
#include "swap.h"

static void perm2str(int perm, char (&str)[4]) {
    str[0] = (perm & VM_USER) ? 'u' : '-';
    str[1] = (perm & VM_PRESENT) ? 'r' : '-';
    str[2] = (perm & VM_WRITE) ? 'w' : '-';
    str[3] = '\0';
}

static void mm_init(MemoryDesc* mm) {
    mm->pgdir = nullptr;
    mm->map_count = 0;
}

namespace vmm {

uintptr_t Manager::mmio_next_va_ = KERNEL_DEVIO_BASE;

bool user_range_valid(MemoryDesc* mm, uintptr_t addr, size_t size, bool write) {
    intr::Guard guard;
    if (!mm || !mm->pgdir || addr < PG_SIZE || addr >= USER_SPACE_TOP || size > USER_SPACE_TOP - addr) {
        return false;
    }
    while (size != 0) {
        if (!pmm::user_address(mm->pgdir, addr, write).ok()) {
            return false;
        }
        size_t chunk = PG_SIZE - (addr & PG_MASK);
        if (chunk > size) {
            chunk = size;
        }
        addr += chunk;
        size -= chunk;
    }
    return true;
}

static Error copy_user(MemoryDesc* mm, uintptr_t user, void* kernel, size_t size, bool to_user) {
    intr::Guard guard;
    ENSURE(kernel && user_range_valid(mm, user, size, to_user));
    auto* bytes = static_cast<uint8_t*>(kernel);
    while (size != 0) {
        auto alias = pmm::user_address(mm->pgdir, user, to_user);
        if (!alias.ok()) {
            return alias.error();
        }
        size_t chunk = PG_SIZE - (user & PG_MASK);
        if (chunk > size) {
            chunk = size;
        }
        if (to_user) {
            memcpy(alias.value(), bytes, chunk);
        } else {
            memcpy(bytes, alias.value(), chunk);
        }
        user += chunk;
        bytes += chunk;
        size -= chunk;
    }
    return Error::None;
}

Error copy_from_user(MemoryDesc* mm, void* dst, uintptr_t src, size_t size) {
    return copy_user(mm, src, dst, size, false);
}

Error copy_to_user(MemoryDesc* mm, uintptr_t dst, const void* src, size_t size) {
    return copy_user(mm, dst, const_cast<void*>(src), size, true);
}

void print_pgdir() {
    const auto* pgdir = Manager::kernel_pgdir();
    cprintf("-------------------- BEGIN --------------------\n");
    cprintf("Root page table at %p\n", pgdir);
    // Dump the present entries in the kernel root table.
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        if (pte_present(pgdir[i])) {
            char perm[4];
            perm2str(pgdir[i] & VM_USER_RW, perm);
            cprintf("  root[%03d] = 0x%016lx %s\n", i, pgdir[i], perm);
        }
    }
    cprintf("--------------------- END ---------------------\n");
}

int pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t addr) {
    if (!mm || !mm->pgdir || addr < PG_SIZE || addr >= USER_SPACE_TOP || (error_code & 1)) {
        return -1;
    }
    addr = round_down(addr, PG_SIZE);
    pte_t* ptep = pmm::get_pte(mm->pgdir, addr, false);
    if (ptep && pte_present(*ptep)) {
        return -1;  // A mapped page fault is not a swap entry.
    }
    if (ptep && *ptep != 0) {
        if ((error_code & 2) != 0 && (*ptep & swap::ENTRY_HAS_PERMISSIONS) != 0 &&
            (*ptep & swap::ENTRY_WRITE) == 0) {
            return -1;
        }
        Page* page = nullptr;
        return swap::in(mm, addr, &page) == Error::None ? 0 : -1;
    }
    Page* page = pmm::pgdir_alloc_page(mm->pgdir, addr, user_page_perm(true));
    if (!page) {
        return -1;
    }
    memset(pmm::page_to_kva(page), 0, PG_SIZE);
    return 0;
}

// Map virtual pages to physical pages in 4-level page table
Error pgdir_init(pde_t* pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
    size_t n = round_up(size, PG_SIZE) / PG_SIZE;
    la = round_down(la, PG_SIZE);
    pa = round_down(pa, PG_SIZE);
    for (; n > 0; n--, la += PG_SIZE, pa += PG_SIZE) {
        pte_t* ptep = pmm::get_pte(pgdir, la, 1);
        if (!ptep) {
            cprintf("vmm: pgdir_init failed to allocate PTE for va=0x%lx\n", la);
            return Error::NoMem;
        }
        *ptep = make_pte_page(pa, perm);
    }
    return Error::None;
}

// -------------------------------------------------------------------------
// MMIO virtual address allocator
// Assigns consecutive virtual addresses starting at KERNEL_DEVIO_BASE.
// The virtual address has NO arithmetic relationship to the physical one.
// -------------------------------------------------------------------------
uintptr_t mmio_map(uintptr_t phys_addr, size_t size, uint32_t perm) {
    size = round_up(size, PG_SIZE);
    uintptr_t va = Manager::mmio_next_va_;
    if (pgdir_init(Manager::kernel_pgdir(), va, size, phys_addr, perm) != Error::None) {
        cprintf("vmm: mmio_map failed for phys=0x%lx size=0x%lx\n", phys_addr, size);
        return 0;
    }
    // Flush TLB for the newly mapped range so that stale entries
    // (e.g. from split 2MB blocks) don't interfere.
    arch_flush_tlb_range(va, size);
    Manager::mmio_next_va_ += size;
    return va;
}

int init() {
    auto* boot_pgdir = __kernel_pg_dir;

    cprintf("vmm: kernel root page table [0x%p]\n", boot_pgdir);

    if (pgdir_init(boot_pgdir, KERNEL_BASE, KERNEL_MEM_SIZE, 0, VM_WRITE) != Error::None) {
        cprintf("vmm: failed to map kernel address space\n");
        return -1;
    }

    arch_flush_tlb_range(KERNEL_BASE, KERNEL_MEM_SIZE);

    mm_init(&Manager::kernel_mm_);
    Manager::kernel_mm_.pgdir = boot_pgdir;

    cprintf("vmm: kernel mapped [0x%lx, 0x%lx)\n", static_cast<uint64_t>(KERNEL_BASE),
            static_cast<uint64_t>(KERNEL_BASE + KERNEL_MEM_SIZE));
    return 0;
}

}  // namespace vmm
