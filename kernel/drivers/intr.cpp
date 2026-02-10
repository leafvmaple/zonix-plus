#include "intr.h"

#include <asm/io.h>

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