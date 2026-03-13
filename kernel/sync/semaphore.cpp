#include "lib/semaphore.h"
#include "lib/lock_guard.h"

void Semaphore::down() {
    while (true) {
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
