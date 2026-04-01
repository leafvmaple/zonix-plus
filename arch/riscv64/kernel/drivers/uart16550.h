#pragma once

/**
 * NS16550A-compatible UART driver for RISC-V (QEMU virt).
 * Physical base address: 0x10000000
 */

#include <base/types.h>

namespace uart16550 {

/* Call at boot to set up MMIO base and enable interrupts */
int init();

/* Called from PLIC external interrupt handler */
void intr();

/* Block-free output (polling) */
void putc(int c);

/* Read a character; returns -1 if none available */
int getc_nonblock();

}  // namespace uart16550
