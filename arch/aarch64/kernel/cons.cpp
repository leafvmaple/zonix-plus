/**
 * @file cons.cpp
 * @brief AArch64 console implementation using PL011 UART + GOP fbcons.
 *
 * Provides a unified input buffer fed by both PL011 (serial) and
 * virtio-keyboard (GUI) interrupt handlers via push_input().
 */

#include "cons/cons.h"
#include "drivers/pl011.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "lib/stdio.h"
#include "lib/waitqueue.h"
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
    int rc = pl011::init();
    if (rc != 0) {
        return rc;
    }
    cprintf("Zonix OS (aarch64) is Loading at [0x%p]...\n", KERNEL_START);
    return 0;
}

int late_init() {
    fbcons::late_init();
    return 0;
}

void push_input(char c) {
    int next = (input_write + 1) % INPUT_BUF_SIZE;
    if (next == input_read)
        return;  // buffer full
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
    pl011::putc(c);
    fbcons::putc(c);
}

}  // namespace cons
