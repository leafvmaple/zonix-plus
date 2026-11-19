#pragma once

#include "page.h"
#include "memlayout.h"

#ifndef __ASSEMBLY__

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

inline constexpr uintptr_t pte_addr(uintptr_t pte) {
    return pte & PTE_ADDR_MASK;
}

// virt_to_phys, phys_to_virt and iterate_pages are shared by all architectures.
#include <kernel/address.h>

#endif /* !__ASSEMBLY__ */
