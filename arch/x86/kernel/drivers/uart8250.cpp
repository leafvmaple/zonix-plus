#include "drivers/uart8250.h"

#include <kernel/config.h>
#include <asm/arch.h>

#include "lib/stdio.h"

#define COM1_PORT 0x3F8

static bool uart_present = false;

namespace uart8250 {

int init() {
    // Scratch register test: verify UART is present
    arch_port_outb(COM1_PORT + 7, 0xA5);
    if (arch_port_inb(COM1_PORT + 7) != 0xA5) {
        cprintf("uart8250: COM1 not detected (scratch test failed)\n");
        return -1;
    }

    arch_port_outb(COM1_PORT + 1, 0x00);  // Disable interrupts
    arch_port_outb(COM1_PORT + 3, 0x80);  // Enable DLAB (set baud rate divisor)
    arch_port_outb(COM1_PORT + 0, 0x01);  // Divisor = 1 (115200 baud)
    arch_port_outb(COM1_PORT + 1, 0x00);  // High byte of divisor
    arch_port_outb(COM1_PORT + 3, 0x03);  // 8 bits, no parity, one stop bit
    arch_port_outb(COM1_PORT + 2, 0xC7);  // Enable FIFO, clear, 14-byte threshold
    arch_port_outb(COM1_PORT + 4, 0x03);  // DTR + RTS

    // Loopback test: enable loopback mode and send a byte
    arch_port_outb(COM1_PORT + 4, 0x1E);  // Set loopback mode
    arch_port_outb(COM1_PORT + 0, 0x42);  // Send test byte
    if (arch_port_inb(COM1_PORT + 0) != 0x42) {
        cprintf("uart8250: COM1 loopback test failed\n");
        return -1;
    }

    // Disable loopback, set normal operation (OUT1+OUT2+RTS+DTR)
    arch_port_outb(COM1_PORT + 4, 0x0F);
    uart_present = true;
    return 0;
}

void putc(int c) {
    if (!uart_present)
        return;
    // Wait for transmit buffer to be empty (bit 5 of LSR)
    if (c == '\n') {
        while ((arch_port_inb(COM1_PORT + 5) & 0x20) == 0) {}
        arch_port_outb(COM1_PORT, '\r');
    }
    while ((arch_port_inb(COM1_PORT + 5) & 0x20) == 0) {}
    arch_port_outb(COM1_PORT, c);
}

}  // namespace uart8250
