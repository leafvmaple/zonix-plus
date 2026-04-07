#include "elf_loader.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/math.h"
#include "mm/pmm.h"
#include "debug/assert.h"

#include <asm/page.h>
#include <asm/mmu.h>
#include <base/elf.h>

namespace elf {

bool is_elf(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(ElfHdr)) {
        return false;
    }
    const auto* eh = reinterpret_cast<const ElfHdr*>(data);
    return eh->e_magic == ELF_MAGIC;
}

Error validate(const ElfHdr* eh, size_t file_size) {
    if (file_size < sizeof(ElfHdr)) {
        cprintf("elf: file too small for ELF header (%d bytes)\n", file_size);
        return Error::Invalid;
    }

    if (eh->e_magic != ELF_MAGIC) {
        cprintf("elf: bad magic: 0x%08x (expected 0x%08x)\n", eh->e_magic, ELF_MAGIC);
        return Error::Invalid;
    }

    if (eh->e_elf[0] != 2) {
        cprintf("elf: not a 64-bit ELF (class=%d)\n", eh->e_elf[0]);
        return Error::Invalid;
    }

    if (eh->e_type != 2) {
        cprintf("elf: not an executable (type=%d)\n", eh->e_type);
        return Error::Invalid;
    }

    if (eh->e_machine != 0x3E) {
        cprintf("elf: wrong architecture (machine=0x%x, expected 0x3E)\n", eh->e_machine);
        return Error::Invalid;
    }

    if (eh->e_phoff == 0 || eh->e_phnum == 0) {
        cprintf("elf: no program headers\n");
        return Error::Invalid;
    }

    size_t ph_end = eh->e_phoff + static_cast<size_t>(eh->e_phnum) * eh->e_phentsize;
    if (ph_end > file_size) {
        cprintf("elf: program header table exceeds file size\n");
        return Error::Invalid;
    }

    return Error::None;
}

uintptr_t load(const uint8_t* data, size_t size, pde_t* pgdir) {
    const auto* eh = reinterpret_cast<const ElfHdr*>(data);

    if (validate(eh, size) != Error::None) {
        return 0;
    }

    cprintf("elf: loading - %d program header(s), entry=0x%lx\n", eh->e_phnum, eh->e_entry);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        size_t ph_offset = eh->e_phoff + i * eh->e_phentsize;
        const auto* ph = reinterpret_cast<const ProgHdr*>(data + ph_offset);

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        if (ph->p_filesz > 0 && ph->p_offset + ph->p_filesz > size) {
            cprintf("elf: segment %d file data out of bounds (offset=0x%lx, filesz=0x%lx, elf_size=0x%lx)\n", i,
                    ph->p_offset, ph->p_filesz, size);
            return 0;
        }

        if (ph->p_va >= KERNEL_BASE) {
            cprintf("elf: segment %d maps to kernel space (va=0x%lx)\n", i, ph->p_va);
            return 0;
        }

        uint32_t perm = VM_USER;
        if (ph->p_flags & ELF_PF_W) {
            perm |= VM_WRITE;
        }

        cprintf("elf:   LOAD seg %d: va=0x%lx, filesz=0x%lx, memsz=0x%lx, perm=%s%s%s\n", i, ph->p_va, ph->p_filesz,
                ph->p_memsz, (ph->p_flags & ELF_PF_R) ? "R" : "-", (ph->p_flags & ELF_PF_W) ? "W" : "-",
                (ph->p_flags & ELF_PF_X) ? "X" : "-");

        uintptr_t seg_start = round_down(ph->p_va, PG_SIZE);
        uintptr_t seg_end = round_up(ph->p_va + ph->p_memsz, PG_SIZE);

        for (uintptr_t va = seg_start; va < seg_end; va += PG_SIZE) {
            pte_t* existing = pmm::get_pte(pgdir, va, false);
            if (existing && (*existing & VM_PRESENT)) {
                *existing |= perm;
                continue;
            }

            Page* page = pmm::pgdir_alloc_page(pgdir, va, perm);
            if (!page) {
                cprintf("elf: failed to allocate page for va=0x%lx\n", va);
                return 0;
            }

            // Zero fresh page (covers BSS and padding)
            memset(phys_to_virt(pmm::page_to_phys(page)), 0, PG_SIZE);
        }

        if (ph->p_filesz > 0) {
            const uint8_t* src = data + ph->p_offset;

            iterate_pages(ph->p_va, ph->p_filesz, [&](uintptr_t va, size_t chunk) {
                pte_t* ptep = pmm::get_pte(pgdir, va, false);
                assert(ptep && (*ptep & VM_PRESENT));

                uint8_t* kva = phys_to_virt<uint8_t>(pte_addr(*ptep));
                memcpy(kva + (va & PG_MASK), src, chunk);

                src += chunk;
            });
        }
    }

    return eh->e_entry;
}

}  // namespace elf
