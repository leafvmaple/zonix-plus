#include "sched.h"
#include "drivers/intr.h"
#include "fs/vfs.h"
#include "lib/stdio.h"
#include <asm/arch.h>

namespace {

Error setup_stdio(fd::Table& files) {
    const char* console_path = "/dev/console";

    for (int expected_fd = 0; expected_fd < 3; expected_fd++) {
        auto file = TRY(vfs::open(console_path));
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
    TRY(proc->files().fork_from(parent->files(), fd::ForkPolicy::Reset));

    TRY(setup_stdio(proc->files()));
    TRY(proc->setup_kernel_stack());
    proc->use_kernel_memory();
    proc->copy_thread(stack, trap_frame);

    // Inherit parent's priority and compute timeslice
    proc->priority = parent->priority;
    proc->time_slice = Scheduler::time_slice(proc->priority);

    {
        intr::Guard guard;
        proc->pid = next_pid_++;
        proc->set_links();
        proc->wakeup();
        // The process table owns the published task until wait detaches and destroys it.
        return proc.release()->pid;
    }
}

Result<int> TaskManager::kernel_thread(KernelThreadEntry fn, void* arg) {
    TrapFrame tf{};

    arch_setup_kthread_tf(&tf, reinterpret_cast<uintptr_t>(kernel_thread_entry), reinterpret_cast<uintptr_t>(fn),
                          reinterpret_cast<uintptr_t>(arg));

    return fork(0, 0, &tf);
}

int TaskManager::exit(int error_code) {
    // Publish zombie state, notify the parent and adopt children atomically.
    intr::Guard irq_guard;
    Task* current = Scheduler::current();
    current->mark_zombie(error_code);

    if (current->parent && current->parent->wait_state) {
        current->parent->wakeup();
    }

    ListNode* children = &current->child_list;
    while (!children->empty()) {
        ListNode* node = children->next_node();
        node->unlink();

        Task* child = Task::from_child_link(node);
        child->parent = init_proc_;
        init_proc_->child_list.add(*node);

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

Result<int> TaskManager::wait(int pid, int* code_store) {
    ENSURE(pid >= 0, Error::Invalid);
    Task* current = Scheduler::current();

    while (true) {
        // Child inspection, sleep registration and scheduling are atomic
        // with respect to child exit on the single-CPU scheduler.
        intr::Guard guard;
        bool has_children{};
        Task* zombie_child{};

        for (auto* node : current->child_list) {
            Task* child = Task::from_child_link(node);
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

            zombie_child->remove_links();
            zombie_child->destroy();

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
