#include "sched.h"

ListNode* sched_cursor{};

const char* SchedulerPolicy::get_name() const {
    return "Priority Round Robin";
}

int SchedulerPolicy::calc_time_slice(int priority) const {
    // Priority 0 (highest) -> 2x base, priority 20 (lowest) -> 0.5x base
    int slice = sched_prio::BASE_TIMESLICE * (sched_prio::MIN_PRIO + 1 - priority) / (sched_prio::DEFAULT + 1);
    if (slice < 1)
        slice = 1;
    return slice;
}

void SchedulerPolicy::tick(TaskStruct* current, TaskStruct* idle) const {
    if (!current || current == idle)
        return;  // idle doesn't consume timeslice

    if (current->time_slice > 0) {
        current->time_slice--;
    }
    if (current->time_slice <= 0) {
        current->need_resched = 1;
    }
}

TaskStruct* SchedulerPolicy::pick_next(ListNode& proc_list, TaskStruct* idle) {
    TaskStruct* next = idle;
    int best_prio = sched_prio::IDLE_PRIO + 1;  // Worse than idle
    ListNode* head = &proc_list;

    if (!sched_cursor || sched_cursor == head) {
        sched_cursor = head->get_next();
    }

    for (auto* node : proc_list.circular_from(sched_cursor)) {
        TaskStruct* proc = TaskStruct::from_list_link(node);
        if (proc->get_state() == ProcessState::Runnable && proc != idle) {
            if (proc->priority < best_prio) {
                next = proc;
                best_prio = proc->priority;
            }
        }
    }

    if (next != idle) {
        sched_cursor = next->list_node.get_next();
    }

    return next;
}
