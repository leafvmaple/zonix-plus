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

static inline int save_impl() {
    if (arch_irq_save() & FL_IF) {
        disable();
        return 1;
    }
    return 0;
}

static inline void restore_impl(int flag) {
    if (flag) {
        enable();
    }
}

Guard::Guard() : flag_(save_impl()) {}
Guard::~Guard() {
    restore_impl(flag_);
}

}  // namespace intr