#include "cons.h"

#include <arch/x86/drivers/i8259.h>
#include <arch/x86/drivers/i8042.h>
#include <arch/x86/segments.h>
#include <arch/x86/io.h>

#include "../drivers/cga.h"
#include "../drivers/pic.h"
#include "../drivers/kdb.h"

#include "stdio.h"

extern uint8_t KERNEL_START[];

void cons_init() {
    cga::init();
    kbd::init();
    cprintf("Zonix OS is Loading in [0x%x]...\n", KERNEL_START);
}

char cons_getc(void) {
    return kbd::getc();
}

void cons_putc(int c) {
    cga::putc(c);
    outb(0xe9, c);  // Also output to Bochs debug port for logging
}
