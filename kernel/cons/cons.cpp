#include "cons.h"

#include <kernel/config.h>
#include <asm/arch.h>

#include "../drivers/cga.h"
#include "../drivers/kdb.h"
#include "../drivers/fbcons.h"
#include "../drivers/serial.h"

#include "stdio.h"

extern uint8_t KERNEL_START[];

namespace cons {

void init() {
    cga::init();
    serial::init();
    kbd::init();

    cprintf("Zonix OS (x86_64) is Loading at [0x%p]...\n", KERNEL_START);
}

char getc() {
    return kbd::getc();
}

void putc(int c) {
    cga::putc(c);
    fbcons::putc(c);
    serial::putc(c);
    arch_port_outb(0xe9, c);
}

}  // namespace cons