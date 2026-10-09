#pragma once

#include <sys/mutex.hpp>
#include "lib/spinlock.h"
#include "lib/waitqueue.h"

// Forward declaration
struct Task;

// Mutex — mutual exclusion lock with ownership tracking.
// Unlike Spinlock, a Mutex blocks (sleeps) when contended.
// Only the holder may unlock it.

class Mutex {
public:
    void lock();
    void unlock();
    bool try_lock();

    [[nodiscard]] bool is_locked() const { return held_; }

private:
    bool held_{false};
    Task* owner_{};
    Spinlock spin_{};
    WaitQueue waitq_{};
};
