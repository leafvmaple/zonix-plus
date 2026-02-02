#include "intr.h"

#include <arch/x86/io.h>

namespace intr {

/* intr enable - enable irq interrupt */
void enable() {
    sti();
}

/* intr disable - disable irq interrupt */
void disable() {
    cli();
}

} // namespace intr