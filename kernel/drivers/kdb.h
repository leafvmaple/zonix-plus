#pragma once

#include <kernel/config.h>
#ifdef CONFIG_PS2KBD

#include <base/types.h>

namespace kbd {

void init(void);
int getc(void);              // Non-blocking: read from hardware (used by IRQ handler)
void intr(void);             // Called from keyboard IRQ: read char, buffer it, wake waiter
char getc_blocking(void);    // Blocking: sleep until a character is available

} // namespace kbd

#endif // CONFIG_PS2KBD