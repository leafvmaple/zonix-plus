/**
 * NS16550A-compatible UART driver for RISC-V.
 *
 * Register stride is board-specific: 1 byte on QEMU virt,
 * 4 bytes on VisionFive 2 (DW APB UART).
 */

#include "uart16550.h"
#include "cons/cons.h"
#include <asm/memlayout.h>
#include <asm/board.h>
#include <asm/arch.h>
#include <base/types.h>

namespace {

/* Physical MMIO base (common for both QEMU virt and JH7110 UART0) */
constexpr uintptr_t UART_PHYS = 0x10000000UL;

/* Virtual MMIO base — accessed via the PGD[256] boot gigapage:
 *   PA 0x00000000 → VA KERNEL_BASE
 *   UART_BASE = KERNEL_BASE + UART_PHYS */
constexpr uintptr_t UART_BASE = UART_PHYS + KERNEL_BASE;

/* Register stride: 1 byte on QEMU virt, 4 bytes on VisionFive 2 */
constexpr uint32_t REG_STRIDE = BOARD_UART_REG_STRIDE;

/* 16550 register offsets (multiplied by stride when accessed) */
constexpr uint32_t REG_RHR = 0; /* Receive  Holding Register (R) */
constexpr uint32_t REG_THR = 0; /* Transmit Holding Register (W) */
constexpr uint32_t REG_IER = 1; /* Interrupt Enable Register     */
constexpr uint32_t REG_IIR = 2; /* Interrupt Identification (R)  */
constexpr uint32_t REG_FCR = 2; /* FIFO Control (W)              */
constexpr uint32_t REG_LSR = 5; /* Line Status                   */
/* DesignWare APB UART (VisionFive 2) status register; reading it clears a
 * busy-detect interrupt. QEMU's 16550 has no such register, but never
 * reports busy-detect either. */
constexpr uint32_t REG_USR = 31;

/* IIR interrupt identification (low four bits) */
constexpr uint8_t IIR_ID_MASK = 0x0F;
constexpr uint8_t IIR_BUSY_DETECT = 0x07; /* DesignWare: LCR written while busy */

/* IER bits */
constexpr uint8_t IER_RX = 0x01; /* Enable Received Data Avail IRQ */

/* LSR bits */
constexpr uint8_t LSR_DR = 0x01;   /* Data Ready     */
constexpr uint8_t LSR_THRE = 0x20; /* THR Empty      */

static volatile uint8_t* reg(uint32_t offset) {
    return reinterpret_cast<volatile uint8_t*>(UART_BASE + offset * REG_STRIDE);
}

static void write_reg(uint32_t offset, uint8_t val) {
    *reg(offset) = val;
    arch_mb();
}

static uint8_t read_reg(uint32_t offset) {
    arch_mb();
    return *reg(offset);
}

void clear_busy_detect() {
    if ((read_reg(REG_IIR) & IIR_ID_MASK) == IIR_BUSY_DETECT)
        static_cast<void>(read_reg(REG_USR));
}

}  // namespace

namespace uart16550 {

int init() {
    /* Firmware has already set the line to 115200 8N1; QEMU ignores it.
     * Writing LCR while a DesignWare UART is transmitting is ignored and
     * raises a busy-detect interrupt, so the line settings stay untouched. */

    /* Disable interrupts while initialising */
    write_reg(REG_IER, 0x00);

    /* Enable and reset FIFOs */
    write_reg(REG_FCR, 0x07);

    clear_busy_detect();

    /* Enable RX-ready interrupt */
    write_reg(REG_IER, IER_RX);

    return 0;
}

void putc(int c) {
    /* Busy-wait until transmit holding register is empty */
    while (!(read_reg(REG_LSR) & LSR_THRE)) {}
    write_reg(REG_THR, static_cast<uint8_t>(c));
}

int getc_nonblock() {
    if (read_reg(REG_LSR) & LSR_DR) {
        return static_cast<int>(read_reg(REG_RHR)) & 0xFF;
    }
    return -1;
}

void intr() {
    clear_busy_detect();
    /* Drain all available bytes in the FIFO */
    while (read_reg(REG_LSR) & LSR_DR) {
        char c = static_cast<char>(read_reg(REG_RHR));
        cons::push_input(c);
    }
}

}  // namespace uart16550
