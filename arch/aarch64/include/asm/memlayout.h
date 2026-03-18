#pragma once

#define KERNEL_BASE     0xFFFF000000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map (vmm::init re-maps as 4 KB pages) */

#ifndef __ASSEMBLY__
#define KERNEL_DEVIO_BASE (KERNEL_BASE + 0x80000000ULL)
#else
#define KERNEL_DEVIO_BASE (KERNEL_BASE + 0x80000000)
#endif
