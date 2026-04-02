#pragma once

/*
 * Board configuration: QEMU virt machine (riscv64)
 *
 * DRAM starts at 0x80000000; OpenSBI occupies 0x80000000..0x801FFFFF.
 * Kernel loaded at 0x80200000.
 * Single hart (hart 0) as boot hart by default.
 */

/* DRAM physical base address */
#define BOARD_DRAM_BASE 0x80000000UL

/* Kernel physical load address (DRAM base + OpenSBI reserved 2MB) */
#define BOARD_KERNEL_PHYS 0x80200000UL

/* UART (NS16550A) register stride in bytes */
#define BOARD_UART_REG_STRIDE 1

/* UART interrupt number on the PLIC */
#define BOARD_UART_IRQ 10

/* PLIC S-mode context index (hart 0 S-mode = context 1) */
#define BOARD_PLIC_S_CONTEXT 1

/* Timer frequency in Hz (QEMU virt = 10 MHz) */
#define BOARD_TIMER_FREQ_HZ 10000000ULL

/* PCI ECAM configuration space physical base */
#define BOARD_PCI_ECAM_PHYS 0x30000000UL

/* PCI ECAM size (1 MB, bus 0 only) */
#define BOARD_PCI_ECAM_SIZE 0x00100000UL
