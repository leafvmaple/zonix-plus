/**
 * NS16550A UART driver for RISC-V QEMU virt machine.
 *
 * The QEMU virt machine maps a 16550-compatible UART at physical
 * 0x10000000.  Registers are 1-byte wide but at 4-byte strides
 * when REGSHIFT = 2 is used.  QEMU actually supports byte accesses
 * at byte-aligned offsets, so we use stride = 1.
 */

#include "uart16550.h"
#include "cons/cons.h"
#include <asm/memlayout.h>
#include <asm/arch.h>
#include <base/types.h>

namespace {

/* Physical MMIO base on QEMU virt RISC-V */
constexpr uintptr_t UART_PHYS = 0x10000000UL;

/* Virtual MMIO base — accessed via the PGD[256] boot gigapage:
 *   PA 0x00000000 → VA KERNEL_BASE
 *   UART_BASE = KERNEL_BASE + UART_PHYS */
constexpr uintptr_t UART_BASE = UART_PHYS + KERNEL_BASE;

/* 16550 register offsets (stride = 1 byte) */
constexpr uint32_t REG_RHR = 0; /* Receive  Holding Register (R) */
constexpr uint32_t REG_THR = 0; /* Transmit Holding Register (W) */
constexpr uint32_t REG_IER = 1; /* Interrupt Enable Register     */
constexpr uint32_t REG_IIR = 2; /* Interrupt Identification (R)  */
constexpr uint32_t REG_FCR = 2; /* FIFO Control (W)              */
constexpr uint32_t REG_LCR = 3; /* Line Control                  */
constexpr uint32_t REG_LSR = 5; /* Line Status                   */

/* LCR bits */
constexpr uint8_t LCR_8N1 = 0x03;  /* 8 data bits, no parity, 1 stop */
constexpr uint8_t LCR_DLAB = 0x80; /* Divisor Latch Access Bit       */

/* IER bits */
constexpr uint8_t IER_RX = 0x01; /* Enable Received Data Avail IRQ */

/* LSR bits */
constexpr uint8_t LSR_DR = 0x01;   /* Data Ready     */
constexpr uint8_t LSR_THRE = 0x20; /* THR Empty      */

static volatile uint8_t* reg(uint32_t offset) {
    return reinterpret_cast<volatile uint8_t*>(UART_BASE + offset);
}

static void write_reg(uint32_t offset, uint8_t val) {
    *reg(offset) = val;
    arch_mb();
}

static uint8_t read_reg(uint32_t offset) {
    arch_mb();
    return *reg(offset);
}

}  // namespace

namespace uart16550 {

int init() {
    /* QEMU's 16550 emulation starts up ready to use, but configure it
     * properly anyway and enable RX interrupts. */

    /* Disable interrupts while initialising */
    write_reg(REG_IER, 0x00);

    /* Set baud rate divisor = 1 (QEMU doesn't care about the value) */
    write_reg(REG_LCR, LCR_DLAB);
    write_reg(0, 0x01); /* DLL = 1 */
    write_reg(1, 0x00); /* DLM = 0 */

    /* 8N1, clear DLAB */
    write_reg(REG_LCR, LCR_8N1);

    /* Enable and reset FIFOs */
    write_reg(REG_FCR, 0x07);

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
    /* Drain all available bytes in the FIFO */
    while (read_reg(REG_LSR) & LSR_DR) {
        char c = static_cast<char>(read_reg(REG_RHR));
        cons::push_input(c);
    }
}

}  // namespace uart16550
