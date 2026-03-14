/**
 * @file cons.cpp
 * @brief AArch64 console implementation using PL011 UART.
 *
 * Replaces the x86 version that uses CGA + PS/2 keyboard + serial.
 * On QEMU virt machine, PL011 UART is at physical 0x09000000.
 */

#include "cons/cons.h"
#include "lib/stdio.h"
#include <asm/page.h>
#include <base/types.h>

extern uint8_t KERNEL_START[];

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

void uart_putc(int c) {
    volatile auto* base = reinterpret_cast<volatile uint8_t*>(uart_base());
    if (c == '\n') {
        // Wait for TX FIFO not full
        while (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_TXFF) {}
        *reinterpret_cast<volatile uint32_t*>(base + UARTDR) = '\r';
    }
    while (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_TXFF) {}
    *reinterpret_cast<volatile uint32_t*>(base + UARTDR) = static_cast<uint32_t>(c);
}

int uart_getc() {
    volatile auto* base = reinterpret_cast<volatile uint8_t*>(uart_base());
    if (*reinterpret_cast<volatile uint32_t*>(base + UARTFR) & UARTFR_RXFE)
        return -1;
    return *reinterpret_cast<volatile uint32_t*>(base + UARTDR) & 0xFF;
}

}  // namespace

namespace cons {

void init() {
    // PL011 is already initialized by QEMU firmware — just print banner
    cprintf("Zonix OS (aarch64) is Loading at [0x%p]...\n", KERNEL_START);
}

char getc() {
    int c;
    while ((c = uart_getc()) < 0) {}
    return static_cast<char>(c);
}

void putc(int c) {
    uart_putc(c);
}

}  // namespace cons
