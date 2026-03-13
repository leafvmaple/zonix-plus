#pragma once

#include "lib/lock_guard.h"
#include "lib/spinlock.h"
#include "lib/waitqueue.h"

// Forward declaration
struct TaskStruct;

// Mutex — mutual exclusion lock with ownership tracking.
// Unlike Spinlock, a Mutex blocks (sleeps) when contended.
// Only the holder may unlock it.

class Mutex {
public:
    void lock();
    void unlock();
    bool try_lock();

    [[nodiscard]] bool is_locked() const { return held_; }

    // LockGuard<T> expects acquire()/release()
    void acquire() { lock(); }
    void release() { unlock(); }

private:
    bool held_{false};
    TaskStruct* owner_{};
    Spinlock spin_{};
    WaitQueue waitq_{};
};
