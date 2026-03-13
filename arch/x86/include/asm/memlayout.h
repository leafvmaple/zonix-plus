#pragma once

// ============================================================================
// Kernel virtual memory layout (x86_64 higher-half at -2GB)
// ============================================================================

#define KERNEL_BASE     0xFFFFFFFF80000000
#define KERNEL_HEADER   0x10000
#define KERNEL_MEM_SIZE 0x38000000

// Start of the MMIO virtual address pool, right after the kernel
// direct-mapped region.  mmio_map() hands out consecutive VAs from here;
// the VA has NO fixed arithmetic relation to the physical address.
#ifndef __ASSEMBLY__
#define KERNEL_DEVIO_BASE (KERNEL_BASE + (uintptr_t)KERNEL_MEM_SIZE)  // 0xFFFFFFFFB8000000
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + KERNEL_MEM_SIZE)
#endif


// ============================================================================
// Boot-time E820 memory probe layout (physical addresses, used by VBR/bootloader)
// ============================================================================

#define E820_MEM_BASE 0x7000
#define E820_MEM_DATA (E820_MEM_BASE + 4)

#define E820_RAM      1
#define E820_RESERVED 2
#define E820_ACPI     3
#define E820_NVS      4


// ============================================================================
// Physical memory map (low region)
// ============================================================================
/* *
      DISK2_END -----------> +---------------------------------+ 0x01FF0000   32MB
      KERNEL_BASE ---------> +---------------------------------+ 0x00100000    1MB
      KERNEL_HEADER -------> +---------------------------------+ 0x00010000   64KB
*     E820_MEM_DATA -------> +---------------------------------+ 0x00007004
*     E820_MEM_BASE -------> +---------------------------------+ 0x00007000   28KB
*                            |              BIOS IVT           | --/--
*     DISK1_BEGIN ---------> +---------------------------------+ 0x00000000
 * */
