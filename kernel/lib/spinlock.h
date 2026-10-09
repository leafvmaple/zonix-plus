#pragma once

#include <asm/arch.h>
#include "drivers/intr.h"
#include <sys/mutex.hpp>

// Simple spinlock for uniprocessor systems.

class Spinlock {
public:
    Spinlock() = default;
    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;

    void lock();
    void unlock();

    [[nodiscard]] bool is_locked() const { return __atomic_load_n(&locked_, __ATOMIC_RELAXED); }

private:
    volatile bool locked_{false};
    uint64_t saved_flags_{};
};
