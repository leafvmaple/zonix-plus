#include "pl011.h"
#include "drivers/mmio.h"
#include <asm/page.h>
#include <asm/mmu.h>
#include <base/types.h>

#include "cons/cons.h"
#include "drivers/gic.h"
#include "drivers/intr.h"

namespace {

// PL011 UART registers (QEMU virt machine)
constexpr uintptr_t UART_PHYS = 0x09000000;

// PL011 register offsets
constexpr uint32_t UARTDR = 0x000;    // Data Register
constexpr uint32_t UARTFR = 0x018;    // Flag Register
constexpr uint32_t UARTIMSC = 0x038;  // Interrupt Mask Set/Clear
constexpr uint32_t UARTICR = 0x044;   // Interrupt Clear Register

constexpr uint32_t UARTFR_TXFF = (1 << 5);    // Transmit FIFO full
constexpr uint32_t UARTFR_RXFE = (1 << 4);    // Receive FIFO empty
constexpr uint32_t UARTIMSC_RXIM = (1 << 4);  // Receive interrupt mask

// QEMU virt: PL011 UART0 = SPI #1 = GIC IntID 33
constexpr uint32_t UART_INTID = 33;

inline uintptr_t uart_base() {
    return reinterpret_cast<uintptr_t>(phys_to_virt<void>(UART_PHYS));
}

}  // namespace

namespace pl011 {

int init() {
    // Enable receive interrupt in PL011
    uint32_t imsc = mmio::read32(uart_base(), UARTIMSC);
    mmio::write32(uart_base(), UARTIMSC, imsc | UARTIMSC_RXIM);
    gic::enable(UART_INTID);

    return 0;
}

void putc(int c) {
    if (c == '\n') {
        while (mmio::read32(uart_base(), UARTFR) & UARTFR_TXFF) {}
        mmio::write32(uart_base(), UARTDR, '\r');
    }
    while (mmio::read32(uart_base(), UARTFR) & UARTFR_TXFF) {}
    mmio::write32(uart_base(), UARTDR, static_cast<uint32_t>(c));
}

int getc() {
    if (mmio::read32(uart_base(), UARTFR) & UARTFR_RXFE)
        return -1;
    return mmio::read32(uart_base(), UARTDR) & 0xFF;
}

void intr() {
    // Clear the receive interrupt
    mmio::write32(uart_base(), UARTICR, UARTIMSC_RXIM);

    // Drain the hardware FIFO into unified console input buffer
    while (!(mmio::read32(uart_base(), UARTFR) & UARTFR_RXFE)) {
        char c = static_cast<char>(mmio::read32(uart_base(), UARTDR) & 0xFF);
        cons::push_input(c);
    }
}

}  // namespace pl011
