#pragma once

// ============================================================================
// Kernel virtual memory layout (AArch64 higher-half)
//
// TTBR1 handles the kernel half: 0xFFFF_0000_0000_0000 and above.
// We place the kernel at KERNEL_BASE so that the direct-mapped
// physical window starts right at the high bit boundary.
// ============================================================================

#define KERNEL_BASE     0xFFFF000000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map window */

#ifndef __ASSEMBLY__
#define KERNEL_DEVIO_BASE (KERNEL_BASE + (uintptr_t)KERNEL_MEM_SIZE)
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + KERNEL_MEM_SIZE)
#endif
