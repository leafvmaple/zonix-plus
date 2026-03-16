#include "pl011.h"

#include <asm/page.h>
#include <base/types.h>

namespace {

// PL011 UART registers (QEMU virt machine)
constexpr uintptr_t UART_PHYS = 0x09000000;

// PL011 register offsets
constexpr uint32_t UARTDR = 0x000;          // Data Register
constexpr uint32_t UARTFR = 0x018;          // Flag Register
constexpr uint32_t UARTFR_TXFF = (1 << 5);  // Transmit FIFO full
constexpr uint32_t UARTFR_RXFE = (1 << 4);  // Receive FIFO empty

volatile uint32_t* uart_base() {
    return phys_to_virt<uint32_t>(UART_PHYS);
}

}  // namespace

namespace pl011 {

void init() {
    // PL011 is already initialized by QEMU firmware
}

void putc(int c) {
    volatile auto* base = reinterpret_cast<volatile uint8_t*>(uart_base());
    if (c == '\n') {
        while (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_TXFF) {}
        *reinterpret_cast<volatile uint32_t*>(base + UARTDR) = '\r';
    }
    while (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_TXFF) {}
    *reinterpret_cast<volatile uint32_t*>(base + UARTDR) = static_cast<uint32_t>(c);
}

int getc() {
    volatile auto* base = reinterpret_cast<volatile uint8_t*>(uart_base());
    if (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_RXFE)
        return -1;
    return *reinterpret_cast<volatile uint32_t*>(base + UARTDR) & 0xFF;
}

}  // namespace pl011
