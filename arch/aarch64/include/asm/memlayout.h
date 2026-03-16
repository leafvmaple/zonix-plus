#pragma once

// ============================================================================
// Kernel virtual memory layout (AArch64 higher-half)
//
// TTBR1 handles the kernel half: 0xFFFF_0000_0000_0000 and above.
// We place the kernel at KERNEL_BASE so that the direct-mapped
// physical window starts right at the high bit boundary.
// ============================================================================

#define KERNEL_BASE     0xFFFF000000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map (vmm::init re-maps as 4 KB pages) */

/*
 * Device MMIO virtual window.  Must NOT overlap with the boot page-table
 * direct map (PUD[0]+PUD[1] = 2 GB), so we place it at KERNEL_BASE + 2 GB.
 */
#ifndef __ASSEMBLY__
#define KERNEL_DEVIO_BASE (KERNEL_BASE + 0x80000000ULL)
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + 0x80000000)
#endif
