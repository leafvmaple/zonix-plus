#include "intr.h"

#include <asm/arch.h>

namespace intr {

/* intr enable - enable irq interrupt */
void enable() {
    arch_irq_enable();
}

/* intr disable - disable irq interrupt */
void disable() {
    arch_irq_disable();
}

} // namespace intr