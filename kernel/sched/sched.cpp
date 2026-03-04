#include "sched.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include <asm/arch.h>
#include <asm/segments.h>
#include <asm/mmu.h>

// External symbols
extern long user_stack[];
extern pde_t* boot_pgdir;
extern MemoryDesc init_mm;  // Global kernel MemoryDesc

// Forward declaration
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);
extern void trapret(void);

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
    return P_ADDR(reinterpret_cast<uintptr_t>(memory->pgdir));
}

void TaskStruct::copy_mm(uint32_t clone_flags) {
    // TODO Full copy implementation
    memory = TaskManager::get_current()->memory;
}

void TaskStruct::copy_thread(uintptr_t esp, TrapFrame* trap_frame) {
    trap_frame = reinterpret_cast<TrapFrame*>(kernel_stack + KSTACK_SIZE) - 1;

    *trap_frame = *trap_frame;
    trap_frame->regs.rax = 0;  // Return value for child
    if (esp != 0) {
        trap_frame->rsp = esp;
    } else {
        // Kernel thread: use the kernel stack (just below the trap frame)
        trap_frame->rsp = reinterpret_cast<uintptr_t>(trap_frame);
    }
    trap_frame->rflags |= 0x200;  // Enable interrupts
    trap_frame->ss = KERNEL_DS;   // Always set SS for iretq

    // Set up context for context switch
    context.rip = reinterpret_cast<uintptr_t>(forkret);
    context.rsp = reinterpret_cast<uintptr_t>(trap_frame);
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
    cprintf("PID  STAT  PPID  KSTACK    MM        NAME\n");
    cprintf("---  ----  ----  --------  --------  ----------------\n");

    ListNode* le = &s_proc_list;
    while ((le = le->get_prev()) != &s_proc_list) {
        TaskStruct* proc = TaskStruct::from_list_link(le);

        cprintf("%c%-3d %-4s  %-4d  %016lx  %016lx  %s\n", (proc == s_current) ? '*' : ' ', proc->pid,
                state_str(proc->state), (proc->parent ? proc->parent->pid : -1), proc->kernel_stack,
                reinterpret_cast<uintptr_t>(proc->memory), proc->name);
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

// Simple round-robin scheduler
void TaskManager::schedule() {
    intr::Guard guard;

    TaskStruct* next = s_idle_proc;
    ListNode* head = &s_proc_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* proc = TaskStruct::from_list_link(le);
        if (proc->state == ProcessState::Runnable) {
            next = proc;
            break;
        }
    }

    if (next != s_current) {
        if (s_current->state == ProcessState::Running) {
            s_current->state = ProcessState::Runnable;
        }
        next->run();
    }
}

int TaskManager::fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame) {
    TaskStruct* proc = new TaskStruct();
    proc->parent = get_current();
    proc->child_list.init();

    proc->setup_kernel_stack();
    proc->copy_mm(clone_flags);
    proc->copy_thread(stack, trap_frame);

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

    tf.cs = KERNEL_CS;
    tf.rflags = FL_IF;                                          // Enable interrupts
    tf.rip = reinterpret_cast<uintptr_t>(kernel_thread_entry);  // Entry wrapper
    tf.regs.rdi = reinterpret_cast<uintptr_t>(fn);              // 1st argument: function
    tf.regs.rsi = reinterpret_cast<uintptr_t>(arg);             // 2nd argument: arg
    tf.rsp = 0;                                                 // Set by copy_thread

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

    trap_frame.cs = KERNEL_CS;
    trap_frame.rflags = FL_IF;  // Enable interrupts
    trap_frame.rip = reinterpret_cast<uintptr_t>(init_main);
    trap_frame.rsp = 0;  // Will be set up by copy_thread

    int ret = fork(0, 0, &trap_frame);
    s_init_proc = find_proc(ret);

    strcpy(s_init_proc->name, "init");
}

namespace sched {

void init() {
    TaskManager::init();
}

}  // namespace sched
