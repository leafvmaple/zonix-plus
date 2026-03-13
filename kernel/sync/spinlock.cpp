#include "lib/spinlock.h"

#include <asm/arch.h>

void Spinlock::acquire() {
    uint64_t flags = arch_irq_save();
    arch_irq_disable();

    while (__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE)) {
        arch_spin_hint();
    }

    saved_flags_ = flags;
}

void Spinlock::release() {
    __atomic_clear(&locked_, __ATOMIC_RELEASE);
    if (saved_flags_ & FL_IF) {
        arch_irq_enable();
    }
}
