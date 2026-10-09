#include "lib/semaphore.h"
#include <sys/mutex.hpp>
#include "drivers/intr.h"

void Semaphore::down() {
    while (true) {
        // The single-CPU scheduler must not run an up() between the
        // condition check and registration in the wait queue.
        intr::Guard irq_guard;
        {
            sys::lock_guard<Spinlock> guard(lock_);
            if (count_ > 0) {
                count_--;
                return;
            }
        }
        waitq_.sleep();
    }
}

bool Semaphore::try_down() {
    sys::lock_guard<Spinlock> guard(lock_);
    if (count_ > 0) {
        count_--;
        return true;
    }
    return false;
}

void Semaphore::up() {
    {
        sys::lock_guard<Spinlock> guard(lock_);
        count_++;
    }
    waitq_.wakeup_one();
}
