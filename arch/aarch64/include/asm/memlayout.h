#pragma once

#define KERNEL_BASE     0xFFFF000000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map (vmm::init re-maps as 4 KB pages) */

#ifndef __ASSEMBLY__
// vmm::init maps physical 0..1 GB (devices) and head.S maps 1..2 GB (RAM);
// physical memory at or above this address has no direct-map address.
#define KERNEL_DIRECT_MAP_END_PA 0x80000000ULL
#define KERNEL_DEVIO_BASE        (KERNEL_BASE + 0x80000000ULL)
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + 0x80000000)
#endif
