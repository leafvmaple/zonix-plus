#include "lib/mutex.h"
#include "lib/lock_guard.h"
#include "debug/assert.h"
#include "sched/sched.h"

void Mutex::lock() {
    while (true) {
        {
            LockGuard<Spinlock> guard(spin_);
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
        LockGuard<Spinlock> guard(spin_);
        assert(held_ && owner_ == sched::current());
        held_ = false;
        owner_ = nullptr;
    }
    waitq_.wakeup_one();
}

bool Mutex::try_lock() {
    LockGuard<Spinlock> guard(spin_);
    if (!held_) {
        held_ = true;
        owner_ = sched::current();
        return true;
    }
    return false;
}
