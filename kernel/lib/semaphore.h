#pragma once

#include "lib/lock_guard.h"
#include "lib/spinlock.h"
#include "lib/waitqueue.h"

// Counting semaphore — blocks when count reaches zero.
// Based on spinlock + WaitQueue.

class Semaphore {
public:
    explicit Semaphore(int initial_count = 0) : count_(initial_count) {}

    void down();
    bool try_down();
    void up();

    [[nodiscard]] int count() const { return count_; }

private:
    int count_;
    Spinlock lock_{};
    WaitQueue waitq_{};
};
