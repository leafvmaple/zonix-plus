#include "sched.h"
#include "mm/vmm.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include <asm/arch.h>
#include <asm/mmu.h>
#include "fs/vfs.h"

extern long user_stack[];

using KernelThreadEntry = int (*)(void*);

#include "cons/shell.h"

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

}  // namespace

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

[[noreturn]] static void kernel_thread_entry(KernelThreadEntry fn, void* arg) {
    int ret = fn(arg);
    TaskManager::exit(ret);
    __builtin_unreachable();
}

// PID 2
static int init_main(void* arg) {
    auto shell_pid_r = TaskManager::kernel_thread(shell::main, nullptr);
    if (!shell_pid_r.ok()) {
        PANIC("init: failed to create shell process!");
    }
    int shell_pid = shell_pid_r.value();

    Task* shell_proc = TaskManager::find_process(shell_pid);
    if (shell_proc) {
        shell_proc->set_name("shell");
    }

    cprintf("init: started shell process (PID %d)\n", shell_pid);

    while (true) {
        int exit_code{};
        auto pid_r = TaskManager::wait(0, &exit_code);
        if (pid_r.ok() && pid_r.value() > 0) {
            cprintf("init: reaped child PID %d (exit code %d)\n", pid_r.value(), exit_code);
        }
    }

    PANIC("init process exited!");
    return 0;
}

void Task::run() {
    Task* current = TaskManager::current();
    if (this != current) {
        intr::Guard guard;

        Task* prev = current;
        TaskManager::set_current(this);
        mark_running();

        uintptr_t next_root_pa = page_table_root_pa();
        uintptr_t previous_root_pa = prev->page_table_root_pa();
        if (next_root_pa != previous_root_pa) {
            arch_load_page_table_root(next_root_pa);
        }

        arch_set_kernel_stack(kernel_stack() + KSTACK_SIZE);

        switch_to(&(prev->context_), &(context_));
    }
}

void Task::wakeup() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Runnable) {
        state_ = TaskState::Runnable;
    }
}

void Task::mark_running() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Running) {
        state_ = TaskState::Running;
    }
}

void Task::mark_zombie(int code) {
    state_ = TaskState::Zombie;
    exit_code = code;
}

void Task::set_name(const char* name) {
    if (!name) {
        return;
    }

    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void Task::sleep() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Sleeping) {
        state_ = TaskState::Sleeping;
    }
}

uintptr_t Task::page_table_root_pa() const {
    assert(memory() != nullptr && memory()->pgdir != nullptr);
    return virt_to_phys(memory()->pgdir);
}

void Task::copy_thread(uintptr_t stack_pointer, TrapFrame* src_tf) {
    trap_frame = reinterpret_cast<TrapFrame*>(kernel_stack() + KSTACK_SIZE) - 1;

    *trap_frame = *src_tf;
    arch_fixup_fork_tf(trap_frame, stack_pointer);

    context_.set_entry(reinterpret_cast<uintptr_t>(forkret));
    context_.set_stack(reinterpret_cast<uintptr_t>(trap_frame));
}

Error Task::setup_kernel_stack() {
    ENSURE(kernel_stack_.empty() && boot_stack_ == 0, Error::Busy);
    kernel_stack_ = TRY(KernelBuffer::alloc(KSTACK_SIZE));
    return Error::None;
}

void Task::set_links() {
    TaskManager::add_process(this);
    if (parent) {
        parent->child_list.add(child_node);
    }
}

void Task::remove_links() {
    TaskManager::remove_process(this);
    child_node.unlink();
}

void Task::destroy() {
    assert(state_ == TaskState::Zombie);
    delete this;
}

int TaskManager::init() {
    if (init_idle() != 0) {
        cprintf("sched: failed to create idle process\n");
        return -1;
    }

    if (init_init_proc() != 0) {
        cprintf("sched: failed to create init process\n");
        return -1;
    }

    cprintf("sched: policy = %s\n", policy_.name());
    cprintf("sched init: idle process PID = 0, init process PID = 1\n");
    return 0;
}

void TaskManager::add_process(Task* proc) {
    hash_node(proc->pid).add(proc->hash_node);
    proc_list_.add(proc->list_node);
    process_count_++;
}

void TaskManager::remove_process(Task* proc) {
    intr::Guard guard;
    policy_.task_removed(*proc);
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
    // Print header (similar to ps aux format)
    cprintf("PID  STAT  PPID  PRIO  SLICE  KSTACK            MM                NAME\n");
    cprintf("---  ----  ----  ----  -----  ----------------  ----------------  ----------------\n");

    for (auto* node : proc_list_.reversed()) {
        Task* proc = Task::from_list_link(node);
        cprintf("%c%-3d %-4s  %-4d  %-4d  %-5d  %016lx  %016lx  %s\n", (proc == current_) ? '*' : ' ', proc->pid,
                state_str(proc->state_), (proc->parent ? proc->parent->pid : -1), proc->priority, proc->time_slice,
                proc->kernel_stack(), reinterpret_cast<uintptr_t>(proc->memory()), proc->name_);
    }

    cprintf("\nTotal processes: %d\n", process_count_);
    cprintf("Current process: %s (PID %d)\n", current_->name_, current_->pid);
    TaskManager::print_stats();
}

void TaskManager::print_stats() {
    cprintf("sched policy: %s\n", policy_.name());
    cprintf("sched stats: ticks=%lu schedule_calls=%lu need_resched_events=%lu\n", tick_count_, schedule_calls_,
            need_resched_events_);
    cprintf("sched stats: ctx_switches=%lu same_task=%lu pick_idle=%lu pick_non_idle=%lu\n", context_switches_,
            same_task_runs_, pick_idle_, pick_non_idle_);
}

uint32_t TaskManager::pid_hash(int x) {
    // Simple hash function
    uint32_t hash = static_cast<uint32_t>(x) * 0x61C88647;
    return hash >> (32 - HASH_SHIFT);
}

void TaskManager::tick() {
    tick_count_++;

    int prev_need_resched = (current_ != nullptr) ? current_->need_resched : 0;
    policy_.tick(current_, idle_proc_);
    if (current_ && !prev_need_resched && current_->need_resched) {
        need_resched_events_++;
    }
}

void TaskManager::schedule() {
    intr::Guard guard;
    schedule_calls_++;

    Task* next = policy_.pick_next(proc_list_, idle_proc_, current_);
    if (next == idle_proc_) {
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

Result<int> TaskManager::fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame) {
    // User-MM cloning requires its own ownership protocol; only kernel-MM borrowing is supported.
    ENSURE(current() && current()->memory() == &vmm::Manager::kernel_mm(), Error::NotSupported);
    auto proc = sys::unique_ptr<Task>(new (sys::nothrow) Task());
    ENSURE(proc, Error::NoMem);
    proc->parent = current();
    if (proc->parent) {
        TRY(proc->files().fork_from(proc->parent->files(), fd::ForkPolicy::Reset));
    } else {
        proc->files().init();
    }

    TRY(setup_stdio(proc->files()));
    TRY(proc->setup_kernel_stack());
    proc->use_kernel_memory();
    proc->copy_thread(stack, trap_frame);

    // Inherit parent's priority and compute timeslice
    proc->priority = current()->priority;
    proc->time_slice = policy_.calc_time_slice(proc->priority);

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
    Task* current = TaskManager::current();
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

    schedule();
    PANIC("TaskManager::exit will not return!");
    return 0;  // Never reached
}

Result<int> TaskManager::wait(int pid, int* code_store) {
    ENSURE(pid >= 0, Error::Invalid);
    Task* current = TaskManager::current();

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
        schedule();
        current->wait_state = 0;
    }
}

// PID 0
int TaskManager::init_idle() {
    auto idle_proc = sys::unique_ptr<Task>(new (sys::nothrow) Task());
    if (!idle_proc) {
        cprintf("sched: init_idle: failed to allocate Task\n");
        return -1;
    }
    idle_proc->boot_stack_ = reinterpret_cast<uintptr_t>(user_stack);
    idle_proc->files().init();
    idle_proc->priority = sched_prio::IDLE_PRIO;
    idle_proc->time_slice = 0;

    // Kernel threads share the memory manager's kernel address space.
    idle_proc->use_kernel_memory();

    // Establish the active root before switches compare task address spaces.
    arch_load_page_table_root(idle_proc->page_table_root_pa());

    idle_proc->set_name("idle");

    idle_proc->wakeup();
    idle_proc_ = idle_proc.release();
    set_current(idle_proc_);
    add_process(idle_proc_);

    return 0;
}

// PID 1
int TaskManager::init_init_proc() {
    TrapFrame trap_frame{};

    arch_setup_kthread_tf(&trap_frame, reinterpret_cast<uintptr_t>(init_main), 0, 0);

    auto ret = fork(0, 0, &trap_frame);
    if (!ret.ok()) {
        cprintf("sched: init_init_proc: fork failed (%s)\n", error_str(ret.error()));
        return -1;
    }

    init_proc_ = find_process(ret.value());
    if (!init_proc_) {
        cprintf("sched: init_init_proc: find_process(%d) returned null\n", ret.value());
        return -1;
    }

    init_proc_->set_name("init");

    return 0;
}

namespace sched {

int init() {
    return TaskManager::init();
}

void schedule() {
    TaskManager::schedule();
}

void tick() {
    TaskManager::tick();
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
    return TaskManager::current();
}

Task* find_process(int pid) {
    return TaskManager::find_process(pid);
}

void print() {
    TaskManager::print();
}

void print_stats() {
    TaskManager::print_stats();
}

}  // namespace sched
