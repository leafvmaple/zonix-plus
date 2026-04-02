#pragma once

/*
 * Board configuration selector for RISC-V 64-bit.
 *
 * Usage: make ARCH=riscv64 BOARD=qemu_virt      (default)
 *        make ARCH=riscv64 BOARD=visionfive2
 *
 * The BOARD variable is translated to -DBOARD_QEMU_VIRT or
 * -DBOARD_VISIONFIVE2 by the Makefile.
 */

#if defined(BOARD_VISIONFIVE2)
#include <asm/board/visionfive2.h>
#elif defined(BOARD_QEMU_VIRT)
#include <asm/board/qemu_virt.h>
#else
/* Default to QEMU virt when no board is specified */
#include <asm/board/qemu_virt.h>
#endif

/*
 * Derived constants (computed from BOARD_DRAM_BASE).
 *
 * With KERNEL_BASE = 0xFFFFFFC000000000 and Sv39 1GB gigapages:
 *   PGD index for DRAM = 256 + (BOARD_DRAM_BASE >> 30)
 *   Identity-map index = BOARD_DRAM_BASE >> 30
 */
#define BOARD_PGD_IDX_MMIO  256
#define BOARD_PGD_IDX_DRAM  (256 + (BOARD_DRAM_BASE >> 30))
#define BOARD_PGD_IDX_IDENT (BOARD_DRAM_BASE >> 30)

/* Boot info is placed 512KB below kernel load address */
#define BOARD_BOOT_INFO_ADDR (BOARD_KERNEL_PHYS - 0x80000UL)
#define BOARD_MMAP_ADDR      (BOARD_BOOT_INFO_ADDR + 0x100UL)
