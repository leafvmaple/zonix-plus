/**
 * @file cons.cpp
 * @brief RISC-V console hardware initialization and NS16550A output.
 *
 * Input buffering and framebuffer output belong to the shared console.
 */

#include "cons/cons.h"
#include <kernel/log.h>
#include "drivers/uart16550.h"
#include "drivers/plic.h"
#include "lib/stdio.h"
#include <asm/trap_numbers.h>
#include <base/types.h>

extern uint8_t KERNEL_START[];

namespace cons {

int arch_init() {
    int rc = uart16550::init();
    if (rc != 0) {
        return rc;
    }

    /* Enable UART IRQ in the PLIC */
    plic::enable(IRQ_UART);

    /* Enable external interrupt delivery to S-mode */
    __asm__ volatile("csrs sie, %0" : : "r"(1UL << 9) : "memory"); /* SEIE bit */

    klog::info("Zonix OS (riscv64)\n");
    klog::debug("boot: kernel start at %p\n", KERNEL_START);
    return 0;
}

void arch_putc(int c) {
    uart16550::putc(c);
}

}  // namespace cons
