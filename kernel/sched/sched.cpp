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
extern pde_t* boot_pgdir;
extern MemoryDesc init_mm;  // Global kernel MemoryDesc

using fnThread = int (*)(void*);

#include "cons/shell.h"

namespace {

int setup_stdio(fd::Table& files) {
    const char* console_path = "/dev/console";

    for (int expected_fd = 0; expected_fd < 3; expected_fd++) {
        vfs::File* file = nullptr;
        if (vfs::open(console_path, &file) != 0 || !file) {
            files.close_all();
            return -1;
        }

        int fd = files.alloc(file);
        if (fd < 0) {
            vfs::close(file);
            files.close_all();
            return -1;
        }

        if (fd != expected_fd) {
            files.close_all();
            return -1;
        }
    }

    return 0;
}

SchedulerPolicy& scheduler() {
    static SchedulerPolicy policy;
    return policy;
}

}  // namespace

static const char* state_str(ProcessState state) {
    switch (state) {
        case ProcessState::Uninit: return "U";    // Uninitialized
        case ProcessState::Sleeping: return "S";  // Sleeping
        case ProcessState::Runnable: return "R";  // Runnable
        case ProcessState::Running: return "R+";  // Running (with +)
        case ProcessState::Zombie: return "Z";    // Zombie
        default: return "?";                      // Unknown
    }
}

static int get_pid() {
    static int next_pid = 1;

    return next_pid++;
}

[[noreturn]] static void kernel_thread_entry(fnThread fn, void* arg) {
    int ret = fn(arg);
    TaskManager::exit(ret);
    __builtin_unreachable();
}

// PID 2
static int init_main(void* arg) {
    int shell_pid = TaskManager::kernel_thread(shell::main, nullptr);
    if (shell_pid <= 0) {
        panic("init: failed to create shell process!");
    }

    TaskStruct* shell_proc = TaskManager::find_proc(shell_pid);
    if (shell_proc) {
        shell_proc->set_name("shell");
    }

    cprintf("init: started shell process (PID %d)\n", shell_pid);

    while (true) {
        int exit_code{};
        int pid = TaskManager::wait(0, &exit_code);
        if (pid > 0) {
            cprintf("init: reaped child PID %d (exit code %d)\n", pid, exit_code);
        }
    }

    panic("init process exited!");
    return 0;
}

void TaskStruct::run() {
    TaskStruct* current = TaskManager::get_current();
    if (this != current) {
        intr::Guard guard;

        TaskStruct* prev = current;
        TaskManager::set_current(this);
        mark_running();

        uintptr_t next_cr3 = get_cr3();
        uintptr_t prev_cr3 = prev->get_cr3();
        if (next_cr3 != prev_cr3) {
            arch_load_cr3(next_cr3);
        }

        arch_switch_rsp0(kernel_stack_ + KSTACK_SIZE);

        switch_to(&(prev->context_), &(context_));
    }
}

void TaskStruct::wakeup() {
    assert(state_ != ProcessState::Zombie);
    if (state_ != ProcessState::Runnable) {
        state_ = ProcessState::Runnable;
    }
}

void TaskStruct::mark_running() {
    assert(state_ != ProcessState::Zombie);
    if (state_ != ProcessState::Running) {
        state_ = ProcessState::Running;
    }
}

void TaskStruct::mark_zombie(int code) {
    state_ = ProcessState::Zombie;
    exit_code = code;
}

void TaskStruct::set_name(const char* name) {
    if (!name) {
        return;
    }

    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void TaskStruct::sleep() {
    assert(state_ != ProcessState::Zombie);
    if (state_ != ProcessState::Sleeping) {
        state_ = ProcessState::Sleeping;
    }
}

uintptr_t TaskStruct::get_cr3() const {
    assert(memory != nullptr && memory->pgdir != nullptr);
    return virt_to_phys(memory->pgdir);
}

void TaskStruct::copy_mm(uint32_t clone_flags) {
    // TODO Full copy implementation
    memory = TaskManager::get_current()->memory;
}

void TaskStruct::copy_thread(uintptr_t esp, TrapFrame* src_tf) {
    trap_frame = reinterpret_cast<TrapFrame*>(kernel_stack_ + KSTACK_SIZE) - 1;

    *trap_frame = *src_tf;
    arch_fixup_fork_tf(trap_frame, esp);

    context_.set_entry(reinterpret_cast<uintptr_t>(forkret));
    context_.set_stack(reinterpret_cast<uintptr_t>(trap_frame));
}

int TaskStruct::setup_kernel_stack() {
    void* stack = kmalloc(KSTACK_SIZE);
    if (!stack) {
        return -1;
    }

    kernel_stack_ = reinterpret_cast<uintptr_t>(stack);
    return 0;
}

void TaskStruct::set_links() {
    TaskManager::add_process(this);
    if (parent) {
        parent->child_list.add(child_node);
    }
}

void TaskStruct::remove_links() {
    TaskManager::remove_process(this);
    child_node.unlink();
}

void TaskStruct::destroy() {
    files().close_all();

    if (kernel_stack_ != reinterpret_cast<uintptr_t>(user_stack)) {
        kfree(reinterpret_cast<void*>(kernel_stack_));
    }

    if (memory && memory != &init_mm) {
        delete memory;
    }
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

    cprintf("sched: policy = %s\n", scheduler().get_name());
    cprintf("sched init: idle process PID = 0, init process PID = 1\n");
    return 0;
}

void TaskManager::add_process(TaskStruct* proc) {
    get_hash_node(proc->pid).add(proc->hash_node);
    s_proc_list.add(proc->list_node);
    s_process_count++;
}

void TaskManager::remove_process(TaskStruct* proc) {
    proc->hash_node.unlink();
    proc->list_node.unlink();
    s_process_count--;
}

TaskStruct* TaskManager::find_proc(int pid) {
    if (pid <= 0) {
        return nullptr;
    }
    ListNode& head = get_hash_node(pid);
    for (auto* node : head) {
        TaskStruct* proc = TaskStruct::from_hash_link(node);
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

    for (auto* node : s_proc_list.reversed()) {
        TaskStruct* proc = TaskStruct::from_list_link(node);
        cprintf("%c%-3d %-4s  %-4d  %-4d  %-5d  %016lx  %016lx  %s\n", (proc == s_current) ? '*' : ' ', proc->pid,
                state_str(proc->state_), (proc->parent ? proc->parent->pid : -1), proc->priority, proc->time_slice,
                proc->kernel_stack_, reinterpret_cast<uintptr_t>(proc->memory), proc->name_);
    }

    cprintf("\nTotal processes: %d\n", s_process_count);
    cprintf("Current process: %s (PID %d)\n", s_current->name_, s_current->pid);
    TaskManager::print_stats();
}

void TaskManager::print_stats() {
    cprintf("sched policy: %s\n", scheduler().get_name());
    cprintf("sched stats: ticks=%lu schedule_calls=%lu need_resched_events=%lu\n", s_tick_count, s_schedule_calls,
            s_need_resched_events);
    cprintf("sched stats: ctx_switches=%lu same_task=%lu pick_idle=%lu pick_non_idle=%lu\n", s_context_switches,
            s_same_task_runs, s_pick_idle, s_pick_non_idle);
}

uint32_t TaskManager::pid_hash(int x) {
    // Simple hash function
    uint32_t hash = static_cast<uint32_t>(x) * 0x61C88647;
    return hash >> (32 - HASH_SHIFT);
}

void TaskManager::tick() {
    s_tick_count++;

    int prev_need_resched = (s_current != nullptr) ? s_current->need_resched : 0;
    scheduler().tick(s_current, s_idle_proc);
    if (s_current && !prev_need_resched && s_current->need_resched) {
        s_need_resched_events++;
    }
}

void TaskManager::schedule() {
    intr::Guard guard;
    s_schedule_calls++;

    TaskStruct* next = scheduler().pick_next(s_proc_list, s_idle_proc);
    if (next == s_idle_proc) {
        s_pick_idle++;
    } else {
        s_pick_non_idle++;
    }

    if (next->time_slice <= 0) {
        next->time_slice = scheduler().calc_time_slice(next->priority);
    }

    if (next != s_current) {
        s_context_switches++;
        if (s_current->get_state() == ProcessState::Running) {
            s_current->wakeup();
        }
        s_current->need_resched = 0;
        next->run();
    } else {
        // Staying on same process — just replenish if needed
        s_same_task_runs++;
        s_current->need_resched = 0;
    }
}

int TaskManager::fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame) {
    TaskStruct* proc = new TaskStruct();
    if (!proc) {
        cprintf("sched: fork: failed to allocate TaskStruct\n");
        return -1;
    }
    proc->parent = get_current();
    if (proc->parent) {
        if (proc->files().fork_from(proc->parent->files(), fd::ForkPolicy::Reset) != 0) {
            cprintf("sched: fork: failed to clone file table\n");
            delete proc;
            return -1;
        }
    } else {
        proc->files().init();
    }

    if (setup_stdio(proc->files()) != 0) {
        cprintf("sched: fork: failed to set up stdio\n");
        delete proc;
        return -1;
    }

    if (proc->setup_kernel_stack() != 0) {
        cprintf("sched: fork: failed to allocate kernel stack\n");
        proc->files().close_all();
        delete proc;
        return -1;
    }
    proc->copy_mm(clone_flags);
    proc->copy_thread(stack, trap_frame);

    // Inherit parent's priority and compute timeslice
    proc->priority = get_current()->priority;
    proc->time_slice = scheduler().calc_time_slice(proc->priority);

    {
        intr::Guard guard;
        proc->pid = get_pid();
        proc->set_links();
    }

    proc->wakeup();
    return proc->pid;
}

int TaskManager::kernel_thread(fnThread fn, void* arg) {
    TrapFrame tf{};

    arch_setup_kthread_tf(&tf, reinterpret_cast<uintptr_t>(kernel_thread_entry), reinterpret_cast<uintptr_t>(fn),
                          reinterpret_cast<uintptr_t>(arg));

    return fork(0, 0, &tf);
}

int TaskManager::exit(int error_code) {
    TaskStruct* current = get_current();
    current->mark_zombie(error_code);

    {
        intr::Guard guard;

        if (current->parent && current->parent->wait_state) {
            current->parent->wakeup();
        }

        ListNode* children = &current->child_list;
        while (!children->empty()) {
            ListNode* node = children->get_next();
            node->unlink();

            TaskStruct* child = TaskStruct::from_child_link(node);
            child->parent = s_init_proc;
            s_init_proc->child_list.add(*node);

            // If child is zombie, it was waiting to be reaped by its original parent.
            // Now that init is the new parent, wake up init so it can reap the zombie.
            // This ensures zombie processes don't linger indefinitely after reparenting.
            if (child->state_ == ProcessState::Zombie) {
                if (s_init_proc->wait_state) {
                    s_init_proc->wakeup();
                }
            }
        }
    }

    schedule();
    panic("TaskManager::exit will not return!");
    return 0;  // Never reached
}

int TaskManager::wait(int pid, int* code_store) {
    TaskStruct* current = get_current();

    while (true) {
        bool has_children{};
        TaskStruct* zombie_child{};

        {
            intr::Guard guard;

            for (auto* node : current->child_list) {
                TaskStruct* child = TaskStruct::from_child_link(node);
                has_children = true;

                if (pid == 0 || child->pid == pid) {
                    if (child->state_ == ProcessState::Zombie) {
                        zombie_child = child;
                        break;
                    }
                }
            }
        }

        if (zombie_child) {
            int child_pid = zombie_child->pid;
            if (code_store) {
                *code_store = zombie_child->exit_code;
            }

            {
                intr::Guard guard;
                zombie_child->remove_links();
            }
            zombie_child->destroy();

            return child_pid;
        }

        if (!has_children) {
            return -1;
        }

        current->wait_state = 1;
        current->sleep();
        schedule();
        current->wait_state = 0;
    }
}

// PID 0
int TaskManager::init_idle() {
    TaskStruct* idle_proc = new TaskStruct();
    if (!idle_proc) {
        cprintf("sched: init_idle: failed to allocate TaskStruct\n");
        return -1;
    }
    idle_proc->wakeup();
    idle_proc->kernel_stack_ = reinterpret_cast<uintptr_t>(user_stack);  // Use boot stack
    idle_proc->files().init();
    idle_proc->priority = sched_prio::IDLE_PRIO;
    idle_proc->time_slice = 0;

    // Idle process uses kernel's init_mm (shared by all kernel threads)
    idle_proc->memory = &init_mm;

    idle_proc->set_name("idle");

    s_idle_proc = idle_proc;
    set_current(idle_proc);
    add_process(idle_proc);

    return 0;
}

// PID 1
int TaskManager::init_init_proc() {
    TrapFrame trap_frame{};

    arch_setup_kthread_tf(&trap_frame, reinterpret_cast<uintptr_t>(init_main), 0, 0);

    int ret = fork(0, 0, &trap_frame);
    if (ret <= 0) {
        cprintf("sched: init_init_proc: fork failed (ret=%d)\n", ret);
        return -1;
    }

    s_init_proc = find_proc(ret);
    if (!s_init_proc) {
        cprintf("sched: init_init_proc: find_proc(%d) returned null\n", ret);
        return -1;
    }

    s_init_proc->set_name("init");

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

int fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf) {
    return TaskManager::fork(clone_flags, stack, tf);
}

int kernel_thread(fnThread fn, void* arg) {
    return TaskManager::kernel_thread(fn, arg);
}

int exit(int error_code) {
    return TaskManager::exit(error_code);
}

int wait(int pid, int* code_store) {
    return TaskManager::wait(pid, code_store);
}

TaskStruct* current() {
    return TaskManager::get_current();
}

TaskStruct* find_proc(int pid) {
    return TaskManager::find_proc(pid);
}

void print() {
    TaskManager::print();
}

void print_stats() {
    TaskManager::print_stats();
}

}  // namespace sched
