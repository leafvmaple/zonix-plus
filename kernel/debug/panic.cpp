#include "stdio.h"
#include "stdarg.h"
#include "../drivers/intr.h"

#include "assert.h"

static int is_panic = 0;

void __panic(const char *file, int line, const char *fmt, ...) {
    if (is_panic) {
        goto panic_dead;
    }
    is_panic = 1;

    va_list ap;
    va_start(ap, fmt);
    cprintf("kernel panic at %s:%d:\n    ", file, line);
    // vcprintf(fmt, ap);
    cprintf("\n");

    va_end(ap);

panic_dead:
    intr::disable();
    while (1) {
        ;
    }
}