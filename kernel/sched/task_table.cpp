#include "sched.h"
#include "drivers/intr.h"
#include "lib/stdio.h"

static const char* state_str(TaskState state) {
    switch (state) {
        case TaskState::Uninit: return "U";    // Uninitialized
        case TaskState::Sleeping: return "S";  // Sleeping
        case TaskState::Runnable: return "R";  // Runnable
        case TaskState::Running: return "R+";  // Running (with +)
        case TaskState::Zombie: return "Z";    // Zombie
        default: return "?";                   // Unknown
    }
}

void TaskManager::add_process(Task* proc) {
    hash_node(proc->pid).add(proc->hash_node);
    proc_list_.add(proc->list_node);
    process_count_++;
}

void TaskManager::remove_process(Task* proc) {
    intr::Guard guard;
    Scheduler::task_removed(*proc);
    proc->hash_node.unlink();
    proc->list_node.unlink();
    process_count_--;
}

Task* TaskManager::find_process(int pid) {
    if (pid <= 0) {
        return nullptr;
    }
    ListNode& head = hash_node(pid);
    for (auto* node : head) {
        Task* proc = Task::from_hash_link(node);
        if (proc->pid == pid) {
            return proc;
        }
    }
    return nullptr;
}

void TaskManager::print() {
    Task* current = Scheduler::current();
    // Print header (similar to ps aux format)
    cprintf("PID  STAT  PPID  PRIO  SLICE  KSTACK            MM                NAME\n");
    cprintf("---  ----  ----  ----  -----  ----------------  ----------------  ----------------\n");

    for (auto* node : proc_list_.reversed()) {
        Task* proc = Task::from_list_link(node);
        cprintf("%c%-3d %-4s  %-4d  %-4d  %-5d  %016lx  %016lx  %s\n", (proc == current) ? '*' : ' ', proc->pid,
                state_str(proc->state_), (proc->parent ? proc->parent->pid : -1), proc->priority, proc->time_slice,
                proc->kernel_stack(), reinterpret_cast<uintptr_t>(proc->memory()), proc->name_);
    }

    cprintf("\nTotal processes: %d\n", process_count_);
    cprintf("Current process: %s (PID %d)\n", current->name_, current->pid);
    Scheduler::print_stats();
}

uint32_t TaskManager::pid_hash(int x) {
    // Simple hash function
    uint32_t hash = static_cast<uint32_t>(x) * 0x61C88647;
    return hash >> (32 - HASH_SHIFT);
}
