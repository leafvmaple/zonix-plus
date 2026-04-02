#pragma once

#include <asm/board.h>

/*
 * RISC-V Sv39 memory layout
 *
 *  Kernel virtual base: 0xFFFFFFC000000000
 *    VPN[2] = 256 (bits [38:30] = 0x100)
 *    All kernel VAs are sign-extended from bit 38.
 *
 *  Physical memory:
 *    DRAM starts at BOARD_DRAM_BASE; OpenSBI occupies base..base+0x1FFFFF.
 *    Kernel loaded at BOARD_KERNEL_PHYS.
 *
 *  virt_to_phys(va) = va - KERNEL_BASE
 *  phys_to_virt(pa) = pa + KERNEL_BASE
 *
 *  Device I/O region:
 *    Boot gigapage PGD[256] maps PA 0x00000000 → VA KERNEL_BASE.
 *    Static MMIO access (UART, PLIC) uses phys_to_virt: VA = PA + KERNEL_BASE.
 *    Dynamic MMIO (vmm::mmio_map) allocates VAs starting at KERNEL_DEVIO_BASE
 *    = KERNEL_BASE + KERNEL_MEM_SIZE, outside the pre-mapped 1GB range.
 */
#define KERNEL_BASE     0xFFFFFFC000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map */

#define KERNEL_DEVIO_BASE (KERNEL_BASE + KERNEL_MEM_SIZE)
