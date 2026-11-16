#include "lib/waitqueue.h"
#include <sys/mutex.hpp>
#include "sched/sched.h"
#include "drivers/intr.h"

void WaitQueue::sleep() {
    intr::Guard irq_guard;  // Do not preempt a task marked sleeping before schedule().
    Entry entry;
    entry.task = sched::current();

    {
        sys::lock_guard<Spinlock> guard(lock_);
        head_.push_back(entry);
        entry.task->sleep();
    }

    sched::schedule();

    {
        sys::lock_guard<Spinlock> guard(lock_);
        entry.node.unlink();
    }
}

void WaitQueue::wakeup_one() {
    sys::lock_guard<Spinlock> guard(lock_);
    if (head_.empty()) {
        return;
    }

    Entry* entry = &head_.front();
    head_.erase(head_.begin());
    entry->task->wakeup();
}

void WaitQueue::wakeup_all() {
    sys::lock_guard<Spinlock> guard(lock_);
    while (!head_.empty()) {
        Entry* entry = &head_.front();
        head_.erase(head_.begin());
        entry->task->wakeup();
    }
}
