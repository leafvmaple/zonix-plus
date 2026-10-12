#include "sched.h"
#include "drivers/intr.h"
#include "lib/stdio.h"

void Scheduler::tick() {
    tick_count_++;

    int prev_need_resched = (current_ != nullptr) ? current_->need_resched : 0;
    policy_.tick(current_, TaskManager::idle_task());
    if (current_ && !prev_need_resched && current_->need_resched) {
        need_resched_events_++;
    }
}

void Scheduler::schedule() {
    intr::Guard guard;
    schedule_calls_++;

    Task* next = policy_.pick_next(TaskManager::task_list(), TaskManager::idle_task(), current_);
    if (next == TaskManager::idle_task()) {
        pick_idle_++;
    } else {
        pick_non_idle_++;
    }

    if (next->time_slice <= 0) {
        next->time_slice = policy_.calc_time_slice(next->priority);
    }

    if (next != current_) {
        context_switches_++;
        if (current_->state() == TaskState::Running) {
            current_->wakeup();
        }
        current_->need_resched = 0;
        next->run();
    } else {
        // Staying on same process — just replenish if needed
        same_task_runs_++;
        current_->need_resched = 0;
    }
}

void Scheduler::print_stats() {
    cprintf("sched policy: %s\n", policy_.name());
    cprintf("sched stats: ticks=%lu schedule_calls=%lu need_resched_events=%lu\n", tick_count_, schedule_calls_,
            need_resched_events_);
    cprintf("sched stats: ctx_switches=%lu same_task=%lu pick_idle=%lu pick_non_idle=%lu\n", context_switches_,
            same_task_runs_, pick_idle_, pick_non_idle_);
}

namespace sched {

int init() {
    return TaskManager::init();
}

void schedule() {
    Scheduler::schedule();
}

void tick() {
    Scheduler::tick();
}

Result<int> fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf) {
    return TaskManager::fork(clone_flags, stack, tf);
}

Result<int> kernel_thread(KernelThreadEntry fn, void* arg) {
    return TaskManager::kernel_thread(fn, arg);
}

int exit(int error_code) {
    return TaskManager::exit(error_code);
}

Result<int> wait(int pid, int* code_store) {
    return TaskManager::wait(pid, code_store);
}

Task* current() {
    return Scheduler::current();
}

Task* find_process(int pid) {
    return TaskManager::find_process(pid);
}

void print() {
    TaskManager::print();
}

void print_stats() {
    Scheduler::print_stats();
}

}  // namespace sched
