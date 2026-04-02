#pragma once

/*
 * Board configuration: StarFive VisionFive 2 (JH7110 SoC)
 *
 * JH7110 has 5 harts: 1× S7 monitor core (hart 0) + 4× U74 (harts 1–4).
 * U-Boot typically boots from hart 1.
 * DRAM starts at 0x40000000.
 */

/* DRAM physical base address */
#define BOARD_DRAM_BASE 0x40000000UL

/* Kernel physical load address (DRAM base + OpenSBI reserved 2MB) */
#define BOARD_KERNEL_PHYS 0x40200000UL

/* UART (DW APB UART / 8250-compatible) register stride in bytes */
#define BOARD_UART_REG_STRIDE 4

/* UART0 interrupt number on the PLIC */
#define BOARD_UART_IRQ 32

/* PLIC S-mode context index (hart 1 S-mode = context 3) */
#define BOARD_PLIC_S_CONTEXT 3

/* Timer frequency in Hz (JH7110 RTC oscillator = 4 MHz) */
#define BOARD_TIMER_FREQ_HZ 4000000ULL

/* PCI ECAM: not yet supported — set to 0 to skip PCI init */
#define BOARD_PCI_ECAM_PHYS 0UL

/* PCI ECAM size */
#define BOARD_PCI_ECAM_SIZE 0UL
