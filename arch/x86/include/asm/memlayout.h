#pragma once

#define KERNEL_BASE     0xFFFFFFFF80000000
#define KERNEL_HEADER   0x10000
#define KERNEL_MEM_SIZE 0x38000000

#ifndef __ASSEMBLY__
#define KERNEL_DEVIO_BASE (KERNEL_BASE + (uintptr_t)KERNEL_MEM_SIZE)  // 0xFFFFFFFFB8000000
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + KERNEL_MEM_SIZE)
#endif

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
