#pragma once

#include "page.h"
#include "memlayout.h"

#ifndef __ASSEMBLY__

/*
 * AArch64 4-level page tables (4KB granule, 48-bit VA):
 *   PGD (Level 0)  -> 512 entries, each covers 512GB
 *   PUD (Level 1)  -> 512 entries, each covers 1GB
 *   PMD (Level 2)  -> 512 entries, each covers 2MB
 *   PTE (Level 3)  -> 512 entries, each covers 4KB
 *
 * Virtual address breakdown (48-bit, TTBR1 high half):
 *   [63:48] all 1s (TTBR1 selection)
 *   [47:39] PGD index  (9 bits)
 *   [38:30] PUD index  (9 bits)
 *   [29:21] PMD index  (9 bits)
 *   [20:12] PTE index  (9 bits)
 *   [11:0]  Page offset (12 bits)
 */

// Page table constants
inline constexpr int PML4X_SHIFT = 39;
inline constexpr int PDPTX_SHIFT = 30;
inline constexpr int PDX_SHIFT = 21;
inline constexpr int PTX_SHIFT = 12;

inline constexpr int ENTRY_NUM = 512;

// virt_to_phys, phys_to_virt and iterate_pages are shared by all architectures.
#include <kernel/address.h>

#endif /* !__ASSEMBLY__ */
