#include "serial.h"

#include <kernel/config.h>
#ifdef CONFIG_SERIAL

#include <arch/x86/io.h>

#define COM1_PORT 0x3F8

namespace serial {

void init() {
    outb(COM1_PORT + 1, 0x00);  // Disable interrupts
    outb(COM1_PORT + 3, 0x80);  // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x01);  // Divisor = 1 (115200 baud)
    outb(COM1_PORT + 1, 0x00);  // High byte of divisor
    outb(COM1_PORT + 3, 0x03);  // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);  // Enable FIFO, clear, 14-byte threshold
    outb(COM1_PORT + 4, 0x03);  // DTR + RTS
}

void putc(int c) {
    // Wait for transmit buffer to be empty (bit 5 of LSR)
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
        ;
    outb(COM1_PORT, c);
}

} // namespace serial

#endif // CONFIG_SERIAL
