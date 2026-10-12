/**
 * @file cons.cpp
 * @brief AArch64 console hardware initialization and PL011 output.
 *
 * Input buffering and framebuffer output belong to the shared console.
 */

#include "cons/cons.h"
#include <kernel/log.h>
#include "drivers/pl011.h"
#include "lib/stdio.h"
#include <base/types.h>

extern uint8_t KERNEL_START[];

namespace cons {

int arch_init() {
    int rc = pl011::init();
    if (rc != 0) {
        return rc;
    }
    klog::info("Zonix OS (aarch64)\n");
    klog::debug("boot: kernel start at %p\n", KERNEL_START);
    return 0;
}

void arch_putc(int c) {
    pl011::putc(c);
}

}  // namespace cons
