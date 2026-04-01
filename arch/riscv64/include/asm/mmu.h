#pragma once

/*
 * RISC-V Sv39 MMU helpers
 * Included by generic kernel code only — do not include in assembly.
 */

#include "memlayout.h"
#include "page.h"

/* Walk constants */
inline constexpr int ADDR_BITS = 39;
inline constexpr int ENTRY_SHIFT = 9;
inline constexpr int ENTRY_NUM = 1 << ENTRY_SHIFT; /* 512 */

/*
 * virt_to_phys / phys_to_virt are defined in page.h so that both
 * assembly-safe headers and C++ code can use them from one place.
 */
