/**
 * @file cons.cpp
 * @brief RISC-V console implementation using NS16550A UART.
 *
 * Provides a ring-buffer input queue fed by the UART RX interrupt
 * handler via push_input().  UART output is polled (blocking putc).
 */

#include "cons/cons.h"
#include "drivers/uart16550.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "drivers/plic.h"
#include "lib/stdio.h"
#include "lib/waitqueue.h"
#include <asm/trap_numbers.h>
#include <base/types.h>

extern uint8_t KERNEL_START[];

namespace {

constexpr size_t INPUT_BUF_SIZE = 128;
char input_buf[INPUT_BUF_SIZE];
volatile int input_read = 0;
volatile int input_write = 0;
WaitQueue input_waitq;

}  // namespace

namespace cons {

int init() {
    int rc = uart16550::init();
    if (rc != 0) {
        return rc;
    }

    /* Enable UART IRQ in the PLIC */
    plic::enable(IRQ_UART);

    /* Enable external interrupt delivery to S-mode */
    __asm__ volatile("csrs sie, %0" : : "r"(1UL << 9) : "memory"); /* SEIE bit */

    cprintf("Zonix OS (riscv64) is Loading at [0x%p]...\n", KERNEL_START);
    return 0;
}

int late_init() {
    fbcons::late_init();
    return 0;
}

void push_input(char c) {
    int next = (input_write + 1) % INPUT_BUF_SIZE;
    if (next == input_read) {
        return; /* buffer full — drop */
    }
    input_buf[input_write] = c;
    input_write = next;
    input_waitq.wakeup_one();
}

char getc() {
    while (true) {
        {
            intr::Guard guard;
            if (input_read != input_write) {
                char c = input_buf[input_read];
                input_read = (input_read + 1) % INPUT_BUF_SIZE;
                return c;
            }
        }
        input_waitq.sleep();
    }
}

void putc(int c) {
    uart16550::putc(c);
    fbcons::putc(c);
}

}  // namespace cons
