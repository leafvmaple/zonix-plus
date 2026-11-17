#include "lib/mutex.h"
#include <sys/mutex.hpp>
#include "debug/assert.h"
#include "sched/sched.h"
#include "drivers/intr.h"

void Mutex::lock() {
    const Error error = acquire(false);
    assert(error == Error::None);
}

Error Mutex::lock_interruptible() {
    return acquire(true);
}

Error Mutex::acquire(bool interruptible) {
    while (true) {
        intr::Guard irq_guard;  // Keep the condition check and sleep atomic.
        Task* task = sched::current();
        ENSURE(!interruptible || !task || !task->termination_requested(), Error::Interrupted);
        {
            sys::lock_guard<Spinlock> guard(spin_);
            if (!held_) {
                held_ = true;
                owner_ = task;
                return Error::None;
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
