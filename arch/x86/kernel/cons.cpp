#include "cons/cons.h"

#include <kernel/config.h>
#include <asm/arch.h>
#include <base/types.h>

#include "drivers/cga.h"
#include "drivers/i8042.h"
#include "drivers/fbcons.h"
#include "drivers/uart8250.h"
#include "drivers/intr.h"

#include "lib/stdio.h"
#include "lib/waitqueue.h"

extern uint8_t KERNEL_START[];

namespace {

constexpr size_t INPUT_BUF_SIZE = 128;
char input_buf[INPUT_BUF_SIZE]{};
volatile int input_read{};
volatile int input_write{};
WaitQueue input_waitq{};

}  // namespace

namespace cons {

int init() {
    int rc{};

    rc = cga::init();
    if (rc != 0)
        cprintf("cons: cga init failed (rc=%d)\n", rc);

    rc = uart8250::init();
    if (rc != 0)
        cprintf("cons: uart8250 init failed (rc=%d), serial output disabled\n", rc);

    rc = i8042::init();
    if (rc != 0)
        cprintf("cons: i8042 init failed (rc=%d), keyboard input disabled\n", rc);

    cprintf("Zonix OS (x86_64) is Loading at [0x%p]...\n", KERNEL_START);
    return 0;
}

int late_init() {
    fbcons::late_init();
    return 0;
}

void push_input(char c) {
    int next = (input_write + 1) % INPUT_BUF_SIZE;
    if (next == input_read) {
        return;  // buffer full
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
    if (!fbcons::is_active())
        cga::putc(c);
    fbcons::putc(c);
    uart8250::putc(c);
    arch_port_outb(0xe9, c);
}

}  // namespace cons