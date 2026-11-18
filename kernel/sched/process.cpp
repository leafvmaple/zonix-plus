#include "sched.h"
#include "cons/cons.h"
#include "drivers/intr.h"
#include "fs/vfs.h"
#include "lib/stdio.h"
#include <asm/arch.h>

void Task::set_cwd(const char* path) {
    strncpy(cwd_, path, sizeof(cwd_));
}

namespace {

Error setup_stdio(fd::Table& files) {
    const char* console_path = "/dev/console";

    for (int expected_fd = 0; expected_fd < 3; expected_fd++) {
        auto file = TRY(vfs::open(console_path, expected_fd == 0 ? O_RDONLY : O_WRONLY));
        const int fd = TRY(files.alloc(sys::move(file)));
        ENSURE(fd == expected_fd, Error::Invalid);
    }

    return Error::None;
}

[[noreturn]] static void kernel_thread_entry(KernelThreadEntry fn, void* arg) {
    int ret = fn(arg);
    TaskManager::exit(ret);
    __builtin_unreachable();
}

}  // namespace

Result<int> TaskManager::fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame) {
    // User-MM cloning requires its own ownership protocol; only kernel-MM borrowing is supported.
    Task* parent = Scheduler::current();
    ENSURE(parent && parent->memory() == &vmm::Manager::kernel_mm(), Error::NotSupported);
    auto proc = sys::unique_ptr<Task>(new (sys::nothrow) Task());
    ENSURE(proc, Error::NoMem);
    proc->parent = parent;
    proc->set_cwd(parent->cwd());
    TRY(proc->files().fork_from(parent->files(), fd::ForkPolicy::Reset));

    TRY(setup_stdio(proc->files()));
    TRY(proc->setup_kernel_stack());
    proc->use_kernel_memory();
    proc->copy_thread(stack, trap_frame);

    // Inherit parent's priority and compute timeslice
    proc->priority = parent->priority;
    proc->time_slice = Scheduler::time_slice(proc->priority);

    return publish(sys::move(proc));
}

Result<int> TaskManager::publish(sys::unique_ptr<Task> proc, bool foreground) {
    intr::Guard guard;
    proc->pid = next_pid_;
    TRY(cons::prepare_spawn(proc->parent, proc.get(), foreground));
    ++next_pid_;
    proc->set_links();
    proc->wakeup();
    return proc.release()->pid;
}

Result<int> TaskManager::spawn_user(sys::unique_ptr<MemoryDesc> memory, uintptr_t entry, uintptr_t stack,
                                    const char* name, bool foreground) {
    Task* parent = Scheduler::current();
    ENSURE(parent && memory && memory->pgdir && entry && stack && name, Error::Invalid);
    auto proc = sys::unique_ptr<Task>(new (sys::nothrow) Task());
    ENSURE(proc, Error::NoMem);
    proc->parent = parent;
    proc->set_cwd(parent->cwd());
    if (parent == idle_proc_)
        TRY(setup_stdio(proc->files()));
    else
        TRY(proc->files().fork_from(parent->files(), fd::ForkPolicy::Spawn));
    TRY(proc->setup_kernel_stack());
    proc->adopt_memory(sys::move(memory));
    proc->setup_user_thread(entry, stack);
    proc->set_name(name);
    proc->priority = parent == idle_proc_ ? sched_prio::DEFAULT : parent->priority;
    proc->time_slice = Scheduler::time_slice(proc->priority);
    return publish(sys::move(proc), foreground);
}

Error TaskManager::terminate(int pid) {
    ENSURE(pid > 0, Error::Invalid);
    intr::Guard guard;
    // All user processes currently share one authority (no UID/credentials yet).
    // Parentage governs wait/reaping, not permission to request termination.
    return request_termination(find_process(pid));
}

Error TaskManager::interrupt_console(int pid) {
    intr::Guard guard;
    return request_termination(find_process(pid));
}

void TaskManager::interrupt_job(int owner) {
    intr::Guard guard;
    for (Task& task : proc_list_)
        if (task.console_job() == owner)
            static_cast<void>(request_termination(&task));
}

void TaskManager::interrupt_pty(const pty::Session* session, int except_pid) {
    intr::Guard guard;
    for (Task& task : proc_list_)
        if (task.pty_session() == session && task.pid != except_pid)
            static_cast<void>(request_termination(&task));
}

bool TaskManager::has_live_job(int owner) {
    for (Task& task : proc_list_)
        if (task.console_job() == owner && task.state() != TaskState::Zombie)
            return true;
    return false;
}

Error TaskManager::request_termination(Task* target) {
    ENSURE(target && target->state() != TaskState::Zombie, Error::NotFound);
    ENSURE(target != init_proc_, Error::Access);
    ENSURE(target->memory() && target->memory() != &vmm::Manager::kernel_mm(), Error::Access);
    target->termination_requested_ = true;
    if (target->state() == TaskState::Sleeping)
        target->wakeup();
    return Error::None;
}

Result<int> TaskManager::kernel_thread(KernelThreadEntry fn, void* arg) {
    TrapFrame tf{};

    arch_setup_kthread_tf(&tf, reinterpret_cast<uintptr_t>(kernel_thread_entry), reinterpret_cast<uintptr_t>(fn),
                          reinterpret_cast<uintptr_t>(arg));

    return fork(0, 0, &tf);
}

int TaskManager::exit(int error_code) {
    Task* current = Scheduler::current();
    if (current == init_proc_)
        PANIC("init exited with code %d", error_code);
    // Descriptions can sleep during close; finish cleanup before publishing zombie state.
    current->files().close_all();
    // Publish zombie state, notify the parent and adopt children atomically.
    intr::Guard irq_guard;
    cons::task_exited(current);
    current->mark_zombie(error_code);

    if (current->parent && current->parent->wait_state) {
        current->parent->wakeup();
    }

    auto& children = current->child_list;
    while (!children.empty()) {
        Task* child = &children.front();
        children.erase(children.begin());
        child->parent = init_proc_;
        init_proc_->child_list.push_front(*child);

        // If child is zombie, it was waiting to be reaped by its original parent.
        // Now that init is the new parent, wake up init so it can reap the zombie.
        // This ensures zombie processes don't linger indefinitely after reparenting.
        if (child->state_ == TaskState::Zombie) {
            if (init_proc_->wait_state) {
                init_proc_->wakeup();
            }
        }
    }

    Scheduler::schedule();
    PANIC("TaskManager::exit will not return!");
    return 0;  // Never reached
}

Result<int> TaskManager::wait_ready(int pid, int* code_store) {
    ENSURE(pid >= 0, Error::Invalid);
    Task* current = Scheduler::current();

    while (true) {
        // Child inspection, sleep registration and scheduling are atomic
        // with respect to child exit on the single-CPU scheduler.
        intr::Guard guard;
        ENSURE(!current->termination_requested(), Error::Interrupted);
        bool has_children{};
        Task* zombie_child{};

        for (Task& task : current->child_list) {
            Task* child = &task;
            if (pid == 0 || child->pid == pid) {
                has_children = true;
                if (child->state_ == TaskState::Zombie) {
                    zombie_child = child;
                    break;
                }
            }
        }

        if (zombie_child) {
            int child_pid = zombie_child->pid;
            if (code_store) {
                *code_store = zombie_child->exit_code;
            }

            return child_pid;
        }

        if (!has_children) {
            return Error::NotFound;
        }

        current->wait_state = 1;
        current->sleep();
        Scheduler::schedule();
        current->wait_state = 0;
    }
}

Result<int> TaskManager::wait(int pid, int* code_store) {
    int child_pid = TRY(wait_ready(pid, code_store));
    // Only this parent can reap its child; retain it through any user status copy.
    intr::Guard guard;
    Task* child = find_process(child_pid);
    assert(child && child->parent == Scheduler::current() && child->state() == TaskState::Zombie);
    child->remove_links();
    child->destroy();
    return child_pid;
}
