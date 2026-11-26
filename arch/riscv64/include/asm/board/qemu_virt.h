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

/* Goldfish real-time clock physical base */
#define BOARD_RTC_PHYS 0x00101000UL

/* PCI ECAM configuration space physical base */
#define BOARD_PCI_ECAM_PHYS 0x30000000UL

/* PCI ECAM size (1 MB, bus 0 only) */
#define BOARD_PCI_ECAM_SIZE 0x00100000UL

/* No DesignWare SD controller; SD cards sit behind PCI SDHCI */
#define BOARD_SD_PHYS     0UL
#define BOARD_SD_CLOCK_HZ 0ULL

/* SBI system reset needs no board preparation */
#define BOARD_SYSCRG_PHYS    0UL
#define BOARD_PMIC_I2C_PHYS  0UL
#define BOARD_PMIC_I2C_CLOCK 0
#define BOARD_PMIC_I2C_RESET 0

/* UEFI provides the framebuffer; QEMU scanout is coherent */
#define BOARD_JH7110_DISPLAY 0
#define BOARD_CCACHE_PHYS    0UL

/* PCI through the firmware-configured ECAM window */
#define BOARD_JH7110_PCIE 0

/* No watchdog is driven */
#define BOARD_JH7110_WATCHDOG     0
#define BOARD_WATCHDOG_PHYS       0UL
#define BOARD_WATCHDOG_APB_CLOCK  0
#define BOARD_WATCHDOG_CORE_CLOCK 0
#define BOARD_WATCHDOG_APB_RESET  0
#define BOARD_WATCHDOG_CORE_RESET 0

/* Networking is VirtIO */
#define BOARD_JH7110_GMAC    0
#define BOARD_GMAC_PHYS      0UL
#define BOARD_GMAC_AHB_CLOCK 0
#define BOARD_GMAC_AXI_CLOCK 0
#define BOARD_GMAC_AXI_RESET 0
#define BOARD_GMAC_AHB_RESET 0

#define BOARD_JH7110_TRNG     0
#define BOARD_TRNG_PHYS       0UL
#define BOARD_STGCRG_PHYS     0UL
#define BOARD_STG_BUS_CLOCK   0
#define BOARD_TRNG_HCLK_CLOCK 0
#define BOARD_TRNG_AHB_CLOCK  0
#define BOARD_TRNG_RESET      0
