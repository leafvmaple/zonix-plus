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
 *    The boot gigapage PGD[BOARD_PGD_IDX_DRAM] maps the first 1GB of DRAM at
 *    phys_to_virt(BOARD_DRAM_BASE); RAM above it has no direct-map address.
 *    Dynamic MMIO (vmm::mmio_map) allocates VAs from KERNEL_DEVIO_BASE, 128GB
 *    above KERNEL_BASE, so it never meets the direct map: on boards whose DRAM
 *    starts at 1GB (VisionFive 2), KERNEL_BASE + 1GB is the kernel's own RAM.
 */
#define KERNEL_BASE     0xFFFFFFC000000000ULL
#define KERNEL_MEM_SIZE 0x40000000 /* 1 GB direct-map */

#define KERNEL_DIRECT_MAP_END_PA (BOARD_DRAM_BASE + 0x40000000UL)
#define KERNEL_DEVIO_BASE        (KERNEL_BASE + 0x2000000000ULL)
