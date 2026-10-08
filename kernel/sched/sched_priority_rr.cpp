#include "sched.h"

const char* SchedulerPolicy::name() const {
    return "Priority Round Robin";
}

int SchedulerPolicy::calc_time_slice(int priority) const {
    // Priority 0 (highest) -> 2x base, priority 20 (lowest) -> 0.5x base
    int slice = sched_prio::BASE_TIMESLICE * (sched_prio::LOWEST_PRIORITY + 1 - priority) / (sched_prio::DEFAULT + 1);
    if (slice < 1)
        slice = 1;
    return slice;
}

void SchedulerPolicy::tick(Task* current, Task* idle) const {
    if (!current || current == idle)
        return;  // idle doesn't consume timeslice

    if (current->time_slice > 0) {
        current->time_slice--;
    }
    if (current->time_slice <= 0) {
        current->need_resched = 1;
    }
}

void SchedulerPolicy::task_removed(const Task& task) {
    if (cursor_ == &task.list_node) {
        cursor_ = nullptr;
    }
}

Task* SchedulerPolicy::pick_next(ListNode& proc_list, Task* idle, Task* current) {
    Task* next = idle;
    int best_prio = sched_prio::IDLE_PRIO + 1;  // Worse than idle
    ListNode* head = &proc_list;

    if (!cursor_ || cursor_ == head) {
        cursor_ = head->next_node();
    }

    for (auto* node : proc_list.circular_from(cursor_)) {
        Task* proc = Task::from_list_link(node);
        if ((proc->state() == TaskState::Runnable || (proc == current && proc->state() == TaskState::Running)) &&
            proc != idle) {
            if (proc->priority < best_prio) {
                next = proc;
                best_prio = proc->priority;
            }
        }
    }

    if (next != idle) {
        cursor_ = next->list_node.next_node();
    }

    return next;
}
