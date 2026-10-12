#include <asm/arch.h>
#include <kernel/log.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "lib/math.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "drivers/intr.h"
#include "trap/trap.h"

#include "vmm.h"
#include "swap.h"

static void perm2str(uintptr_t entry, char (&str)[4]) {
    str[0] = pte_user(entry) ? 'u' : '-';
    str[1] = pte_present(entry) ? 'r' : '-';
    str[2] = pte_writable(entry) ? 'w' : '-';
    str[3] = '\0';
}

static void mm_init(MemoryDesc* mm) {
    mm->pgdir = nullptr;
    mm->map_count = 0;
}

namespace vmm {


bool user_range_valid(MemoryDesc* mm, uintptr_t user_va, size_t byte_count, bool write) {
    intr::Guard guard;
    if (!mm || !mm->pgdir || user_va < PG_SIZE || user_va >= USER_SPACE_TOP || byte_count > USER_SPACE_TOP - user_va) {
        return false;
    }
    while (byte_count != 0) {
        if (!pmm::user_address(mm->pgdir, user_va, write).ok()) {
            return false;
        }
        size_t chunk_bytes = PG_SIZE - (user_va & PG_MASK);
        if (chunk_bytes > byte_count) {
            chunk_bytes = byte_count;
        }
        user_va += chunk_bytes;
        byte_count -= chunk_bytes;
    }
    return true;
}

static Error copy_user(MemoryDesc* mm, uintptr_t user_va, void* kernel_buffer, size_t byte_count, bool to_user) {
    intr::Guard guard;
    ENSURE(kernel_buffer && user_range_valid(mm, user_va, byte_count, to_user));
    auto* bytes = static_cast<uint8_t*>(kernel_buffer);
    while (byte_count != 0) {
        auto alias = pmm::user_address(mm->pgdir, user_va, to_user);
        if (!alias.ok()) {
            return alias.error();
        }
        size_t chunk_bytes = PG_SIZE - (user_va & PG_MASK);
        if (chunk_bytes > byte_count) {
            chunk_bytes = byte_count;
        }
        if (to_user) {
            memcpy(alias.value(), bytes, chunk_bytes);
        } else {
            memcpy(bytes, alias.value(), chunk_bytes);
        }
        user_va += chunk_bytes;
        bytes += chunk_bytes;
        byte_count -= chunk_bytes;
    }
    return Error::None;
}

Error copy_from_user(MemoryDesc* mm, void* kernel_dst, uintptr_t user_src_va, size_t byte_count) {
    return copy_user(mm, user_src_va, kernel_dst, byte_count, false);
}

Error copy_to_user(MemoryDesc* mm, uintptr_t user_dst_va, const void* kernel_src, size_t byte_count) {
    return copy_user(mm, user_dst_va, const_cast<void*>(kernel_src), byte_count, true);
}

void print_pgdir() {
    const auto* pgdir = Manager::kernel_pgdir();
    cprintf("-------------------- BEGIN --------------------\n");
    cprintf("Root page table at %p\n", pgdir);
    // Dump the present entries in the kernel root table.
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        if (pte_present(pgdir[i])) {
            char perm[4];
            perm2str(pgdir[i], perm);
            cprintf("  root[%03d] = 0x%016lx %s\n", i, pgdir[i], perm);
        }
    }
    cprintf("--------------------- END ---------------------\n");
}

int pg_fault(MemoryDesc* mm, uint32_t error_code, uintptr_t fault_va) {
    if (!mm || !mm->pgdir || fault_va < PG_SIZE || fault_va >= USER_SPACE_TOP ||
        (error_code & trap::PAGE_FAULT_PROTECTION) != 0 || (error_code & trap::PAGE_FAULT_USER) == 0) {
        return -1;
    }
    fault_va = round_down(fault_va, PG_SIZE);
    pte_t* ptep = pmm::get_pte(mm->pgdir, fault_va, false);
    // ELF and stacks are eagerly mapped. Until VMA-backed demand allocation
    // exists, an empty PTE grants no right to create a new user mapping.
    if (!ptep || *ptep == 0 || pte_present(*ptep)) {
        return -1;
    }
    if ((*ptep & swap::ENTRY_HAS_PERMISSIONS) == 0 ||
        ((error_code & trap::PAGE_FAULT_WRITE) != 0 && (*ptep & swap::ENTRY_WRITE) == 0) ||
        ((error_code & trap::PAGE_FAULT_EXECUTE) != 0 && (*ptep & swap::ENTRY_EXECUTE) == 0)) {
        return -1;
    }
    Page* page = nullptr;
    return swap::in(mm, fault_va, &page) == Error::None ? 0 : -1;
}

// Map a byte range through the architecture's page table levels.
Error map_physical_range(pde_t* pgdir, uintptr_t va, size_t byte_count, uintptr_t pa, uint32_t perm) {
    size_t page_count = round_up(byte_count, PG_SIZE) / PG_SIZE;
    va = round_down(va, PG_SIZE);
    pa = round_down(pa, PG_SIZE);
    for (; page_count > 0; page_count--, va += PG_SIZE, pa += PG_SIZE) {
        pte_t* ptep = pmm::get_pte(pgdir, va, 1);
        if (!ptep) {
            klog::error("vmm: map_physical_range failed to allocate PTE for va=0x%lx\n", va);
            return Error::NoMem;
        }
        *ptep = make_pte_page(pa, perm);
    }
    return Error::None;
}

int init() {
    auto* boot_pgdir = __kernel_pg_dir;

    klog::debug("vmm: kernel root page table [0x%p]\n", boot_pgdir);

    if (map_physical_range(boot_pgdir, KERNEL_BASE, KERNEL_MEM_SIZE, 0, VM_WRITE) != Error::None) {
        klog::error("vmm: failed to map kernel address space\n");
        return -1;
    }

    arch_invalidate_tlb_range(KERNEL_BASE, KERNEL_MEM_SIZE);

    mm_init(&Manager::kernel_mm_);
    Manager::kernel_mm_.pgdir = boot_pgdir;

    klog::debug("vmm: kernel mapped [0x%lx, 0x%lx)\n", static_cast<uint64_t>(KERNEL_BASE),
                static_cast<uint64_t>(KERNEL_BASE + KERNEL_MEM_SIZE));
    return 0;
}

}  // namespace vmm
