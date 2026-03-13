#include "sched.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include <asm/arch.h>
#include <asm/mmu.h>

// External symbols
extern long user_stack[];
extern pde_t* boot_pgdir;
extern MemoryDesc init_mm;  // Global kernel MemoryDesc

// Shell process entry point (defined in shell.cpp)
#include "cons/shell.h"

// TaskManager static member definitions (only non-inline members)
ListNode TaskManager::s_proc_list{};
ListNode TaskManager::s_hash_list[TaskManager::HASH_LIST_SIZE]{};

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

// Allocate a unique PID
static int get_pid(void) {
    static int next_pid = 1;

    return next_pid++;
}

// Free kernel stack
static void free_kstack(TaskStruct* proc) {
    pmm::free_page(pmm::kva2page(reinterpret_cast<void*>(proc->kernel_stack)));
}

// Wrapper called by new kernel threads via iretq.
// After iretq, RDI = fn pointer, RSI = arg (set up in TrapFrame by kernel_thread).
// This function signature matches the x86_64 calling convention:
// rdi = first param, rsi = second param.
__attribute__((noreturn)) static void kernel_thread_entry(int (*fn)(void*), void* arg) {
    int ret = fn(arg);
    TaskManager::exit(ret);
    __builtin_unreachable();
}

// Init process main function — fork shell, then reap children
static int init_main(void* arg) {
    // Fork the shell as a separate process (PID 2)
    int shell_pid = TaskManager::kernel_thread(shell::main, nullptr);
    if (shell_pid <= 0) {
        panic("init: failed to create shell process!");
    }

    // Find and name the shell process
    TaskStruct* shell_proc = TaskManager::find_proc(shell_pid);
    if (shell_proc) {
        strcpy(shell_proc->name, "shell");
    }

    cprintf("init: started shell process (PID %d)\n", shell_pid);

    // Init's job: wait for child processes (reap zombies)
    while (true) {
        int exit_code = 0;
        int pid = TaskManager::wait(0, &exit_code);
        if (pid > 0) {
            cprintf("init: reaped child PID %d (exit code %d)\n", pid, exit_code);
        }
    }

    panic("init process exited!");
    return 0;
}

// ============================================================================
// TaskStruct member functions (order matches sched.h)
// ============================================================================

void TaskStruct::run() {
    TaskStruct* current = TaskManager::get_current();
    if (this != current) {
        intr::Guard guard;

        TaskStruct* prev = current;
        TaskManager::set_current(this);
        state = ProcessState::Running;

        uintptr_t next_cr3 = get_cr3();
        uintptr_t prev_cr3 = prev->get_cr3();
        if (next_cr3 != prev_cr3) {
            arch_load_cr3(next_cr3);
        }

        // Update kernel stack pointer for privilege transitions
        arch_switch_rsp0(this->kernel_stack + KSTACK_SIZE);

        switch_to(&(prev->context), &(context));
    }
}

void TaskStruct::wakeup() {
    assert(state != ProcessState::Zombie);
    if (state != ProcessState::Runnable) {
        state = ProcessState::Runnable;
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
    trap_frame = reinterpret_cast<TrapFrame*>(kernel_stack + KSTACK_SIZE) - 1;

    *trap_frame = *src_tf;
    arch_fixup_fork_tf(trap_frame, esp);

    // Set up context for context switch
    context.set_entry(reinterpret_cast<uintptr_t>(forkret));
    context.set_stack(reinterpret_cast<uintptr_t>(trap_frame));
}

int TaskStruct::setup_kernel_stack() {
    Page* page = pmm::alloc_page();
    if (!page) {
        return -1;
    }

    kernel_stack = reinterpret_cast<uintptr_t>(pmm::page2kva(page));
    return 0;
}

// Set process relationships (parent-child)
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
    if (kernel_stack != reinterpret_cast<uintptr_t>(user_stack)) {
        free_kstack(this);
    }
    // Free user address space if this is not a kernel thread
    if (memory && memory != &init_mm) {
        delete memory;
    }
    delete this;
}

// ============================================================================
// TaskManager static functions (order matches sched.h)
// ============================================================================

void TaskManager::init() {
    s_proc_list.init();
    for (size_t i = 0; i < HASH_LIST_SIZE; i++) {
        s_hash_list[i].init();
    }

    init_idle();
    init_init_proc();

    cprintf("sched init: idle process PID = 0, init process PID = 1\n");
}

void TaskManager::add_process(TaskStruct* proc) {
    get_hash_node(proc->pid).add(proc->hash_node);
    s_proc_list.add(proc->list_node);
    nr_process++;
}

void TaskManager::remove_process(TaskStruct* proc) {
    proc->hash_node.unlink();
    proc->list_node.unlink();
    nr_process--;
}

TaskStruct* TaskManager::find_proc(int pid) {
    if (pid <= 0) {
        return nullptr;
    }
    ListNode* head = &get_hash_node(pid);
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* proc = TaskStruct::from_hash_link(le);
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

    ListNode* le = &s_proc_list;
    while ((le = le->get_prev()) != &s_proc_list) {
        TaskStruct* proc = TaskStruct::from_list_link(le);

        cprintf("%c%-3d %-4s  %-4d  %-4d  %-5d  %016lx  %016lx  %s\n", (proc == s_current) ? '*' : ' ', proc->pid,
                state_str(proc->state), (proc->parent ? proc->parent->pid : -1), proc->priority, proc->time_slice,
                proc->kernel_stack, reinterpret_cast<uintptr_t>(proc->memory), proc->name);
    }

    cprintf("\nTotal processes: %d\n", nr_process);
    cprintf("Current process: %s (PID %d)\n", s_current->name, s_current->pid);
}

uint32_t TaskManager::pid_hash(int x) {
    // Simple hash function
    uint32_t hash = static_cast<uint32_t>(x) * 0x61C88647;
    return hash >> (32 - HASH_SHIFT);
}

// ============================================================================
// TaskManager scheduling and process lifecycle (static methods)
// ============================================================================

// Compute timeslice from priority (higher priority = longer slice)
int TaskManager::calc_timeslice(int priority) {
    // Priority 0 (highest) -> 2x base, priority 20 (lowest) -> 0.5x base
    int slice = sched_prio::BASE_TIMESLICE * (sched_prio::MIN_PRIO + 1 - priority) / (sched_prio::DEFAULT + 1);
    if (slice < 1)
        slice = 1;
    return slice;
}

// Called from timer ISR every tick — decrement timeslice, request reschedule
void TaskManager::tick() {
    if (!s_current) {
        return;
    }
    if (s_current == s_idle_proc) {
        return;  // idle doesn't consume timeslice
    }

    if (s_current->time_slice > 0) {
        s_current->time_slice--;
    }
    if (s_current->time_slice <= 0) {
        s_current->need_resched = 1;
    }
}

// Priority-aware round-robin scheduler
void TaskManager::schedule() {
    intr::Guard guard;

    TaskStruct* next = s_idle_proc;
    int best_prio = sched_prio::IDLE_PRIO + 1;  // Worse than idle
    ListNode* head = &s_proc_list;

    // Start scanning from the round-robin cursor to ensure fairness.
    // The cursor points to the node AFTER the last scheduled process.
    if (!s_sched_cursor || s_sched_cursor == head) {
        s_sched_cursor = head->get_next();
    }

    // Scan the entire list starting from cursor, wrapping around
    ListNode* start = s_sched_cursor;
    ListNode* le = start;
    bool wrapped = false;
    while (true) {
        if (le == head) {
            le = head->get_next();
            if (le == head) {
                break;  // Empty list
            }
            if (le == start) {
                break;  // Full wrap
            }
            wrapped = true;
            continue;
        }
        if (wrapped && le == start) {
            break;  // Completed full scan
        }

        TaskStruct* proc = TaskStruct::from_list_link(le);
        if (proc->state == ProcessState::Runnable && proc != s_idle_proc) {
            // Among runnable processes, prefer higher priority (lower number).
            // For equal priority, pick the first one found (round-robin effect).
            if (proc->priority < best_prio) {
                next = proc;
                best_prio = proc->priority;
            }
        }
        le = le->get_next();
    }

    // Advance cursor past the chosen process for next time
    if (next != s_idle_proc) {
        s_sched_cursor = next->list_node.get_next();
    }

    // Replenish timeslice for the next process if exhausted
    if (next->time_slice <= 0) {
        next->time_slice = calc_timeslice(next->priority);
    }

    if (next != s_current) {
        if (s_current->state == ProcessState::Running) {
            s_current->state = ProcessState::Runnable;
        }
        s_current->need_resched = 0;
        next->run();
    } else {
        // Staying on same process — just replenish if needed
        s_current->need_resched = 0;
    }
}

int TaskManager::fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame) {
    TaskStruct* proc = new TaskStruct();
    proc->parent = get_current();
    proc->child_list.init();

    proc->setup_kernel_stack();
    proc->copy_mm(clone_flags);
    proc->copy_thread(stack, trap_frame);

    // Inherit parent's priority and compute timeslice
    proc->priority = get_current()->priority;
    proc->time_slice = calc_timeslice(proc->priority);

    {
        intr::Guard guard;
        proc->pid = get_pid();
        proc->set_links();
    }

    proc->wakeup();
    return proc->pid;
}

int TaskManager::kernel_thread(int (*fn)(void*), void* arg) {
    TrapFrame tf{};

    arch_setup_kthread_tf(&tf, reinterpret_cast<uintptr_t>(kernel_thread_entry), reinterpret_cast<uintptr_t>(fn),
                          reinterpret_cast<uintptr_t>(arg));

    return fork(0, 0, &tf);
}

int TaskManager::exit(int error_code) {
    TaskStruct* current = get_current();
    current->state = ProcessState::Zombie;
    current->exit_code = error_code;

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
            if (child->state == ProcessState::Zombie) {
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
        bool has_children = false;
        TaskStruct* zombie_child = nullptr;

        {
            intr::Guard guard;

            ListNode* head = &current->child_list;
            for (auto* le = head->get_next(); le != head; le = le->get_next()) {
                TaskStruct* child = TaskStruct::from_child_link(le);
                has_children = true;

                if (pid == 0 || child->pid == pid) {
                    if (child->state == ProcessState::Zombie) {
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
        current->state = ProcessState::Sleeping;
        schedule();
        current->wait_state = 0;
    }
}

// Initialize idle process (PID 0)
void TaskManager::init_idle() {
    TaskStruct* idle_proc = new TaskStruct();
    idle_proc->state = ProcessState::Runnable;
    idle_proc->kernel_stack = reinterpret_cast<uintptr_t>(user_stack);  // Use boot stack
    idle_proc->child_list.init();
    idle_proc->priority = sched_prio::IDLE_PRIO;
    idle_proc->time_slice = 0;

    // Idle process uses kernel's init_mm (shared by all kernel threads)
    idle_proc->memory = &init_mm;

    strcpy(idle_proc->name, "idle");

    s_idle_proc = idle_proc;
    set_current(idle_proc);
    add_process(idle_proc);
}

// Create init process using fork (PID 1)
void TaskManager::init_init_proc() {
    TrapFrame trap_frame{};

    arch_setup_kthread_tf(&trap_frame, reinterpret_cast<uintptr_t>(init_main), 0, 0);

    int ret = fork(0, 0, &trap_frame);
    s_init_proc = find_proc(ret);

    strcpy(s_init_proc->name, "init");
}

namespace sched {

void init() {
    TaskManager::init();
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

int kernel_thread(int (*fn)(void*), void* arg) {
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

}  // namespace sched
