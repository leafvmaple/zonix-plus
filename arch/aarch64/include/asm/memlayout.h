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

// ============================================================================
// Physical-to-virtual helpers  (within the kernel direct-map window)
// ============================================================================

#ifndef __ASSEMBLY__

#include <base/types.h>

static inline void* phys_to_virt(uintptr_t pa) {
    return reinterpret_cast<void*>(pa + KERNEL_BASE);
}

template<typename T>
static inline T* phys_to_virt(uintptr_t pa) {
    return reinterpret_cast<T*>(pa + KERNEL_BASE);
}

static inline uintptr_t virt_to_phys(void* va) {
    return reinterpret_cast<uintptr_t>(va) - KERNEL_BASE;
}

#endif /* !__ASSEMBLY__ */
