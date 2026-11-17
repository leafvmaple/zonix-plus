#include "sched.h"
#include "drivers/intr.h"
#include "lib/stdio.h"
#include <abi/process.h>

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
    hash_list(proc->pid).push_front(*proc);
    proc_list_.push_front(*proc);
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
    for (Task& task : hash_list(pid)) {
        Task* proc = &task;
        if (proc->pid == pid) {
            return proc;
        }
    }
    return nullptr;
}

Result<bool> TaskManager::next_process(int after_pid, ProcessInfo& info) {
    ENSURE(after_pid >= -1, Error::Invalid);
    intr::Guard guard;
    Task* selected{};
    for (Task& task : proc_list_) {
        if (task.pid > after_pid && (!selected || task.pid < selected->pid))
            selected = &task;
    }
    if (!selected)
        return false;
    info = ProcessInfo{};
    info.pid = selected->pid;
    info.ppid = selected->parent ? selected->parent->pid : -1;
    info.state = static_cast<unsigned int>(selected->state());
    info.kind = selected->memory() == &vmm::Manager::kernel_mm() ? PROCESS_KIND_KERNEL : PROCESS_KIND_USER;
    strncpy(info.name, selected->name_, sizeof(info.name) - 1);
    return true;
}

static_assert(static_cast<unsigned int>(TaskState::Uninit) == PROCESS_STATE_UNINIT &&
              static_cast<unsigned int>(TaskState::Sleeping) == PROCESS_STATE_SLEEPING &&
              static_cast<unsigned int>(TaskState::Runnable) == PROCESS_STATE_RUNNABLE &&
              static_cast<unsigned int>(TaskState::Running) == PROCESS_STATE_RUNNING &&
              static_cast<unsigned int>(TaskState::Zombie) == PROCESS_STATE_ZOMBIE);

void TaskManager::print() {
    Task* current = Scheduler::current();
    // Print header (similar to ps aux format)
    cprintf("PID  STAT  PPID  PRIO  SLICE  KSTACK            MM                NAME\n");
    cprintf("---  ----  ----  ----  -----  ----------------  ----------------  ----------------\n");

    for (Task& task : proc_list_.reversed()) {
        Task* proc = &task;
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
