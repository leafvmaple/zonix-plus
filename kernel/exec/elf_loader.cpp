/**
 * ELF64 loader — pure parsing and segment loading.
 *
 * No process creation, no filesystem access, no scheduling.
 * See exec.cpp for the generic execution framework.
 */

#include "elf_loader.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/math.h"
#include "mm/pmm.h"

#include <asm/mmu.h>
#include <base/elf.h>

namespace elf {

// ============================================================================
// Quick magic check
// ============================================================================

bool is_elf(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(elfhdr))
        return false;
    auto* eh = reinterpret_cast<const elfhdr*>(data);
    return eh->e_magic == ELF_MAGIC;
}

// ============================================================================
// Full ELF header validation
// ============================================================================

int validate(const elfhdr* eh, size_t file_size) {
    if (file_size < sizeof(elfhdr)) {
        cprintf("elf: file too small for ELF header (%d bytes)\n", file_size);
        return -1;
    }

    if (eh->e_magic != ELF_MAGIC) {
        cprintf("elf: bad magic: 0x%08x (expected 0x%08x)\n", eh->e_magic, ELF_MAGIC);
        return -1;
    }

    if (eh->e_elf[0] != 2) {
        cprintf("elf: not a 64-bit ELF (class=%d)\n", eh->e_elf[0]);
        return -1;
    }

    if (eh->e_type != 2) {
        cprintf("elf: not an executable (type=%d)\n", eh->e_type);
        return -1;
    }

    if (eh->e_machine != 0x3E) {
        cprintf("elf: wrong architecture (machine=0x%x, expected 0x3E)\n", eh->e_machine);
        return -1;
    }

    if (eh->e_phoff == 0 || eh->e_phnum == 0) {
        cprintf("elf: no program headers\n");
        return -1;
    }

    size_t ph_end = eh->e_phoff + (size_t)eh->e_phnum * eh->e_phentsize;
    if (ph_end > file_size) {
        cprintf("elf: program header table exceeds file size\n");
        return -1;
    }

    return 0;
}

// ============================================================================
// Segment loading
// ============================================================================

uintptr_t load(const uint8_t* data, size_t size, pde_t* pgdir) {
    const auto* eh = reinterpret_cast<const elfhdr*>(data);

    if (validate(eh, size) != 0) {
        return 0;
    }

    cprintf("elf: loading - %d program header(s), entry=0x%lx\n", eh->e_phnum, eh->e_entry);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        size_t ph_offset = eh->e_phoff + i * eh->e_phentsize;
        const auto* ph = reinterpret_cast<const proghdr*>(data + ph_offset);

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        // Validate segment file data bounds
        if (ph->p_filesz > 0 && ph->p_offset + ph->p_filesz > size) {
            cprintf("elf: segment %d file data out of bounds "
                    "(offset=0x%lx, filesz=0x%lx, elf_size=0x%lx)\n",
                    i, ph->p_offset, ph->p_filesz, size);
            return 0;
        }

        // Reject kernel-space addresses
        if (ph->p_va >= KERNEL_BASE) {
            cprintf("elf: segment %d maps to kernel space (va=0x%lx)\n", i, ph->p_va);
            return 0;
        }

        // Page permissions
        uint32_t perm = PTE_U;
        if (ph->p_flags & ELF_PF_W)
            perm |= PTE_W;

        cprintf("elf:   LOAD seg %d: va=0x%lx, filesz=0x%lx, "
                "memsz=0x%lx, perm=%s%s%s\n",
                i, ph->p_va, ph->p_filesz, ph->p_memsz, (ph->p_flags & ELF_PF_R) ? "R" : "-",
                (ph->p_flags & ELF_PF_W) ? "W" : "-", (ph->p_flags & ELF_PF_X) ? "X" : "-");

        // Allocate and map pages for this segment
        uintptr_t seg_start = round_down(ph->p_va, PG_SIZE);
        uintptr_t seg_end = round_up(ph->p_va + ph->p_memsz, PG_SIZE);

        for (uintptr_t va = seg_start; va < seg_end; va += PG_SIZE) {
            pte_t* existing = pmm::get_pte(pgdir, va, false);
            if (existing && (*existing & PTE_P)) {
                *existing |= (perm | PTE_P);
                continue;
            }

            Page* page = pmm::pgdir_alloc_page(pgdir, va, perm);
            if (!page) {
                cprintf("elf: failed to allocate page for va=0x%lx\n", va);
                return 0;
            }

            // Zero fresh page (covers BSS and padding)
            pte_t* ptep = pmm::get_pte(pgdir, va, false);
            if (ptep && (*ptep & PTE_P)) {
                void* kva = K_ADDR(PTE_ADDR(*ptep));
                memset(kva, 0, PG_SIZE);
            }
        }

        // Copy file data into the mapped pages
        if (ph->p_filesz > 0) {
            const uint8_t* src = data + ph->p_offset;
            size_t remaining = ph->p_filesz;
            uintptr_t dst_va = ph->p_va;

            while (remaining > 0) {
                pte_t* ptep = pmm::get_pte(pgdir, dst_va, false);
                if (!ptep || !(*ptep & PTE_P)) {
                    cprintf("elf: PTE not present for va=0x%lx\n", dst_va);
                    return 0;
                }

                uintptr_t pa = PTE_ADDR(*ptep);
                uint8_t* kva = static_cast<uint8_t*>(K_ADDR(pa));
                size_t page_off = dst_va & PG_MASK;
                size_t chunk = PG_SIZE - page_off;
                if (chunk > remaining) {
                    chunk = remaining;
                }

                memcpy(kva + page_off, src, chunk);
                src += chunk;
                dst_va += chunk;
                remaining -= chunk;
            }
        }
    }

    return eh->e_entry;
}

}  // namespace elf
