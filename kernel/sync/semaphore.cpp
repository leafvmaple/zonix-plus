#include "lib/semaphore.h"
#include "lib/lock_guard.h"
#include "drivers/intr.h"

void Semaphore::down() {
    while (true) {
        // The single-CPU scheduler must not run an up() between the
        // condition check and registration in the wait queue.
        intr::Guard irq_guard;
        {
            LockGuard<Spinlock> guard(lock_);
            if (count_ > 0) {
                count_--;
                return;
            }
        }
        waitq_.sleep();
    }
}

bool Semaphore::try_down() {
    LockGuard<Spinlock> guard(lock_);
    if (count_ > 0) {
        count_--;
        return true;
    }
    return false;
}

void Semaphore::up() {
    {
        LockGuard<Spinlock> guard(lock_);
        count_++;
    }
    waitq_.wakeup_one();
}
