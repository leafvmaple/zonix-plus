#include "lib/waitqueue.h"
#include <sys/mutex.hpp>
#include "sched/sched.h"
#include "drivers/intr.h"

struct Entry {
    Task* task{};
    ListNode node{};

    static Entry* from_node(ListNode* n) {
        return reinterpret_cast<Entry*>(reinterpret_cast<char*>(n) - __builtin_offsetof(Entry, node));
    }
};

void WaitQueue::sleep() {
    intr::Guard irq_guard;  // Do not preempt a task marked sleeping before schedule().
    Entry entry;
    entry.task = sched::current();

    {
        sys::lock_guard<Spinlock> guard(lock_);
        head_.add_before(entry.node);
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

    ListNode* first = head_.next_node();
    Entry* entry = Entry::from_node(first);
    first->unlink();
    entry->task->wakeup();
}

void WaitQueue::wakeup_all() {
    sys::lock_guard<Spinlock> guard(lock_);
    while (!head_.empty()) {
        ListNode* node = head_.next_node();
        Entry* entry = Entry::from_node(node);
        node->unlink();
        entry->task->wakeup();
    }
}
