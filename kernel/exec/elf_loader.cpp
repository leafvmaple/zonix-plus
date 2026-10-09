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
    if (!data || size < sizeof(ElfHeader)) {
        return false;
    }
    const auto* eh = reinterpret_cast<const ElfHeader*>(data);
    return eh->is_valid();
}

Error validate(const ElfHeader* eh, size_t file_size) {
    ENSURE_LOG(eh, Error::Invalid, "elf: null ELF header");
    ENSURE_LOG(file_size >= sizeof(ElfHeader), Error::Invalid, "elf: file too small (%d bytes)", file_size);

    ENSURE_LOG(eh->is_valid(), Error::Invalid,
               "elf: invalid ELF header (magic=0x%08x, class=%d, version=%d, machine=0x%04x)", eh->e_magic,
               eh->e_elf[0], eh->e_version, eh->e_machine);

    ENSURE_LOG(eh->is_executable(), Error::Invalid, "elf: not an executable ELF file");

    ENSURE(eh->e_ehsize == sizeof(ElfHeader) && eh->e_phentsize == sizeof(ProgramHeader));
    ENSURE(eh->e_phoff >= sizeof(ElfHeader) && eh->e_phoff <= file_size);
    size_t table_size = static_cast<size_t>(eh->e_phnum) * sizeof(ProgramHeader);
    ENSURE(table_size <= file_size - eh->e_phoff);
    ENSURE(eh->e_entry >= PG_SIZE && eh->e_entry < USER_SPACE_TOP);

    const auto* data = reinterpret_cast<const uint8_t*>(eh);
    bool executable_entry = false;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        ProgramHeader ph{};
        memcpy(&ph, data + eh->e_phoff + static_cast<size_t>(i) * sizeof(ProgramHeader), sizeof(ph));
        if (ph.p_type != ELF_PT_LOAD) {
            continue;
        }
        ENSURE(ph.p_filesz <= ph.p_memsz);
        ENSURE(ph.p_offset <= file_size && ph.p_filesz <= file_size - ph.p_offset);
        if (ph.p_memsz == 0) {
            continue;
        }
        ENSURE(ph.p_va >= PG_SIZE && ph.p_va < USER_SPACE_TOP);
        ENSURE(ph.p_memsz <= USER_SPACE_TOP - ph.p_va);
        uintptr_t end = ph.p_va + ph.p_memsz;
        // exec installs the user stack separately; segments must not overlap it.
        ENSURE(end <= USER_STACK_TOP - USER_STACK_SIZE || ph.p_va >= USER_STACK_TOP);
        if ((ph.p_flags & ELF_PF_X) && eh->e_entry >= ph.p_va && eh->e_entry < end) {
            executable_entry = true;
        }
    }
    ENSURE(executable_entry);

    return Error::None;
}

Result<uintptr_t> load(const uint8_t* data, size_t size, pde_t* pgdir) {
    ENSURE(data && pgdir);
    const auto* eh = reinterpret_cast<const ElfHeader*>(data);

    TRY(validate(eh, size));

    cprintf("elf: loading - %d program header(s), entry=0x%lx\n", eh->e_phnum, eh->e_entry);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        size_t ph_offset = eh->e_phoff + i * eh->e_phentsize;
        ProgramHeader header{};
        memcpy(&header, data + ph_offset, sizeof(header));
        const auto* ph = &header;

        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        uint32_t perm = user_page_perm((ph->p_flags & ELF_PF_W) != 0, (ph->p_flags & ELF_PF_X) != 0);

        cprintf("elf:   LOAD seg %d: va=0x%lx, filesz=0x%lx, memsz=0x%lx, perm=%s%s%s\n", i, ph->p_va, ph->p_filesz,
                ph->p_memsz, (ph->p_flags & ELF_PF_R) ? "R" : "-", (ph->p_flags & ELF_PF_W) ? "W" : "-",
                (ph->p_flags & ELF_PF_X) ? "X" : "-");

        uintptr_t seg_start = round_down(ph->p_va, PG_SIZE);
        uintptr_t seg_end = round_up(ph->p_va + ph->p_memsz, PG_SIZE);

        for (uintptr_t va = seg_start; va < seg_end; va += PG_SIZE) {
            pte_t* existing = pmm::get_pte(pgdir, va, false);
            if (existing && pte_present(*existing)) {
                uint32_t merged = merge_user_page_perm(*existing, perm);
                *existing = make_pte_page(pte_addr(*existing), merged);
                continue;
            }

            Page* page = pmm::alloc_and_map_page(pgdir, va, perm);
            if (!page) {
                cprintf("elf: failed to allocate page for va=0x%lx\n", va);
                return Error::NoMem;
            }

            // Zero fresh page (covers BSS and padding)
            memset(phys_to_virt(pmm::page_to_phys(page)), 0, PG_SIZE);
        }

        if (ph->p_filesz > 0) {
            const uint8_t* src = data + ph->p_offset;

            iterate_pages(ph->p_va, ph->p_filesz, [&](uintptr_t va, size_t chunk) {
                pte_t* ptep = pmm::get_pte(pgdir, va, false);
                assert(ptep && pte_present(*ptep));

                uint8_t* kva = phys_to_virt<uint8_t>(pte_addr(*ptep));
                memcpy(kva + (va & PG_MASK), src, chunk);

                src += chunk;
            });
        }
    }

    return eh->e_entry;
}

}  // namespace elf
