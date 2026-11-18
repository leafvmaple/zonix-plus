#include "cons/cons.h"
#include <kernel/log.h>

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
        klog::warn("cons: cga init failed (rc=%d)\n", rc);

    rc = uart8250::init();
    if (rc != 0)
        klog::warn("cons: uart8250 init failed (rc=%d), serial output disabled\n", rc);

    rc = i8042::init();
    if (rc != 0)
        klog::warn("cons: i8042 init failed (rc=%d), keyboard input disabled\n", rc);

    klog::info("Zonix OS (x86_64)\n");
    klog::debug("boot: kernel start at %p\n", KERNEL_START);
    return 0;
}

void arch_putc(int c) {
    uart8250::putc(c);
    arch_port_write8(0xe9, c);
}

void arch_text_putc(int c) {
    cga::putc(c);
}
TerminalSize arch_screen_size() {
    return cga::size();
}
void arch_move_cursor(unsigned int row, unsigned int col) {
    cga::move_cursor(row, col);
}
void arch_move_column(unsigned int col) {
    cga::move_column(col);
}
void arch_clear_screen() {
    cga::clear();
}
void arch_erase_line(unsigned int mode) {
    cga::erase_line(mode);
}
void arch_set_cursor_visible(bool visible) {
    cga::set_cursor_visible(visible);
}

}  // namespace cons