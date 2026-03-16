#include "cons/cons.h"

#include <kernel/config.h>
#include <asm/arch.h>

#include "drivers/cga.h"
#include "drivers/i8042.h"
#include "drivers/fbcons.h"
#include "drivers/uart8250.h"

#include "lib/stdio.h"

extern uint8_t KERNEL_START[];

namespace cons {

void init() {
    cga::init();
    uart8250::init();
    i8042::init();

    cprintf("Zonix OS (x86_64) is Loading at [0x%p]...\n", KERNEL_START);
}

void late_init() {
    fbcons::late_init();
}

char getc() {
    return i8042::getc_blocking();
}

void putc(int c) {
    cga::putc(c);
    fbcons::putc(c);
    uart8250::putc(c);
    arch_port_outb(0xe9, c);
}

}  // namespace cons