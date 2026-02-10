#include "cons.h"

#include <kernel/config.h>
#include <arch/x86/io.h>

#ifdef CONFIG_CGA
#include "../drivers/cga.h"
#endif
#ifdef CONFIG_PS2KBD
#include "../drivers/kdb.h"
#endif
#ifdef CONFIG_FBCONS
#include "../drivers/fbcons.h"
#endif
#include "../drivers/serial.h"

#include "stdio.h"

extern uint8_t KERNEL_START[];

void cons_init() {
#ifdef CONFIG_CGA
    cga::init();
#endif
#ifdef CONFIG_SERIAL
    serial::init();
#endif
#ifdef CONFIG_PS2KBD
    kbd::init();
#endif
    cprintf("Zonix OS (x86_64) is Loading at [0x%p]...\n", KERNEL_START);
}

char cons_getc(void) {
#ifdef CONFIG_PS2KBD
    return kbd::getc();
#else
    return -1;
#endif
}

void cons_putc(int c) {
#ifdef CONFIG_CGA
    cga::putc(c);
#endif
#ifdef CONFIG_FBCONS
    fbcons::putc(c);
#endif
#ifdef CONFIG_SERIAL
    serial::putc(c);
#endif
#ifdef CONFIG_BOCHS_DBG
    outb(0xe9, c);
#endif
}
