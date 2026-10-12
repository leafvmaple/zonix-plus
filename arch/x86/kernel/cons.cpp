#include "cons/cons.h"

#include <kernel/config.h>
#include <asm/arch.h>
#include <base/types.h>

#include "drivers/cga.h"
#include "drivers/i8042.h"
#include "drivers/fbcons.h"
#include "drivers/uart8250.h"

#include "lib/stdio.h"

extern uint8_t KERNEL_START[];

namespace cons {

int arch_init() {
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

void arch_putc(int c) {
    if (!fbcons::is_active())
        cga::putc(c);
    uart8250::putc(c);
    arch_port_write8(0xe9, c);
}

}  // namespace cons