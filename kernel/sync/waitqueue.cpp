#include "lib/waitqueue.h"
#include "lib/lock_guard.h"
#include "sched/sched.h"

struct Entry {
    TaskStruct* task{};
    ListNode node{};

    static Entry* from_node(ListNode* n) {
        return reinterpret_cast<Entry*>(reinterpret_cast<char*>(n) - __builtin_offsetof(Entry, node));
    }
};

void WaitQueue::sleep() {
    Entry entry;
    entry.task = sched::current();

    {
        LockGuard<Spinlock> guard(lock_);
        head_.add_before(entry.node);
        entry.task->state = ProcessState::Sleeping;
    }

    sched::schedule();

    {
        LockGuard<Spinlock> guard(lock_);
        entry.node.unlink();
    }
}

void WaitQueue::wakeup_one() {
    LockGuard<Spinlock> guard(lock_);
    if (head_.empty()) {
        return;
    }

    ListNode* first = head_.get_next();
    Entry* entry = Entry::from_node(first);
    first->unlink();
    entry->task->wakeup();
}

void WaitQueue::wakeup_all() {
    LockGuard<Spinlock> guard(lock_);
    while (!head_.empty()) {
        ListNode* node = head_.get_next();
        Entry* entry = Entry::from_node(node);
        node->unlink();
        entry->task->wakeup();
    }
}
