/**
 * @file cons.cpp
 * @brief AArch64 console implementation using PL011 UART.
 */

#include "cons/cons.h"
#include "drivers/pl011.h"
#include "lib/stdio.h"
#include <base/types.h>

extern uint8_t KERNEL_START[];

namespace cons {

void init() {
    pl011::init();
    cprintf("Zonix OS (aarch64) is Loading at [0x%p]...\n", KERNEL_START);
}

void late_init() {}

char getc() {
    int c;
    while ((c = pl011::getc()) < 0) {}
    return static_cast<char>(c);
}

void putc(int c) {
    pl011::putc(c);
}

}  // namespace cons
