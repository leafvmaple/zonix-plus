#pragma once

#include <asm/arch.h>
#include "drivers/intr.h"
#include "lib/lock_guard.h"

// Simple spinlock for uniprocessor systems.

class Spinlock {
public:
    void acquire();
    void release();

    [[nodiscard]] bool is_locked() const { return __atomic_load_n(&locked_, __ATOMIC_RELAXED); }

private:
    volatile bool locked_{false};
    uint64_t saved_flags_{};
};
