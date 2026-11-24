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

/* PLIC S-mode context index. Hart 0 (S7) has only an M-mode context (0),
 * so hart 1 has M = 1 and S = 2, as in the JH7110 device tree. */
#define BOARD_PLIC_S_CONTEXT 2

/* Timer frequency in Hz (JH7110 RTC oscillator = 4 MHz) */
#define BOARD_TIMER_FREQ_HZ 4000000ULL

/* PCI ECAM: not yet supported — set to 0 to skip PCI init */
#define BOARD_PCI_ECAM_PHYS 0UL

/* No real-time clock driver for this board yet */
#define BOARD_RTC_PHYS 0UL

/* PCI ECAM size */
#define BOARD_PCI_ECAM_SIZE 0UL

/* TF card slot: DesignWare MSHC "sdio1", fed by a 49.5 MHz clock (PLL2 / 24)
 * that firmware leaves enabled. */
#define BOARD_SD_PHYS     0x16020000UL
#define BOARD_SD_CLOCK_HZ 49500000ULL

/* OpenSBI powers off and resets the board through the AXP15060 PMIC on I2C5.
 * Starting the OS gates that controller's APB clock and resets its
 * configuration, so the kernel restores both before an SBI system reset. */
#define BOARD_SYSCRG_PHYS    0x13020000UL
#define BOARD_PMIC_I2C_PHYS  0x12050000UL
#define BOARD_PMIC_I2C_CLOCK 143 /* JH7110_SYSCLK_I2C5_APB */
#define BOARD_PMIC_I2C_RESET 81  /* JH7110_SYSRST_I2C5_APB */

/* The kernel drives HDMI itself (DC8200 + Innosilicon TX); the display reads
 * memory around the SiFive composable cache, flushed through its controller. */
#define BOARD_JH7110_DISPLAY 1
#define BOARD_CCACHE_PHYS    0x02010000UL
