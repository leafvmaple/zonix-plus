#include "lib/mutex.h"
#include <sys/mutex.hpp>
#include "debug/assert.h"
#include "sched/sched.h"
#include "drivers/intr.h"

void Mutex::lock() {
    while (true) {
        intr::Guard irq_guard;  // Keep the condition check and sleep atomic.
        {
            sys::lock_guard<Spinlock> guard(spin_);
            if (!held_) {
                held_ = true;
                owner_ = sched::current();
                return;
            }
        }
        waitq_.sleep();
    }
}

void Mutex::unlock() {
    {
        sys::lock_guard<Spinlock> guard(spin_);
        assert(held_ && owner_ == sched::current());
        held_ = false;
        owner_ = nullptr;
    }
    waitq_.wakeup_one();
}

bool Mutex::try_lock() {
    sys::lock_guard<Spinlock> guard(spin_);
    if (!held_) {
        held_ = true;
        owner_ = sched::current();
        return true;
    }
    return false;
}
