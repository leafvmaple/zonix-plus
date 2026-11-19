#pragma once

/*
 * RISC-V Sv39 MMU helpers
 * Included by generic kernel code only — do not include in assembly.
 */

#include "memlayout.h"
#include "page.h"

/* Walk constants */
inline constexpr int ENTRY_SHIFT = 9;
inline constexpr int ENTRY_NUM = 1 << ENTRY_SHIFT; /* 512 */

// virt_to_phys, phys_to_virt and iterate_pages are shared by all architectures.
#include <kernel/address.h>
