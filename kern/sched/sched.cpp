#include "sched.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../include/stdio.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../drivers/intr.h"
#include "../cons/shell.h"
#include "../debug/assert.h"
#include <base/types.h>
#include <arch/x86/segments.h>
#include <arch/x86/mmu.h>
#include <arch/x86/io.h>

// External symbols
extern long user_stack[];
extern pde_t* boot_pgdir;
extern MemoryDesc init_mm;  // Global kernel MemoryDesc

// Forward declaration
extern "C" void forkret(void);
extern "C" void switch_to(struct Context *from, struct Context *to);
extern void trapret(void);

// TaskManager static member definitions (only non-inline members)
ListNode TaskManager::s_proc_list{};
ListNode TaskManager::s_hash_list[TaskManager::HASH_LIST_SIZE]{};

static const char *state_str(ProcessState state) {
    switch (state) {
        case ProcessState::Uninit:    return "U";  // Uninitialized
        case ProcessState::Sleeping:  return "S";  // Sleeping
        case ProcessState::Runnable:  return "R";  // Runnable
        case ProcessState::Running:   return "R+"; // Running (with +)
        case ProcessState::Zombie:    return "Z";  // Zombie
        default: return "?";  // Unknown
    }
}

// Allocate a unique PID
static int get_pid(void) {
    static int next_pid = 1;

    return next_pid++;
}

// Free kernel stack
static void free_kstack(TaskStruct *proc) {
    free_page(kva2page((void *)proc->m_kernel_stack));
}

// Init process main function (kernel thread entry point)
static int init_main(void *arg) {
    shell_prompt();

    while (1) {
        TaskManager::schedule();
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
        InterruptsGuard guard;
        
        TaskStruct *prev = current;
        TaskManager::set_current(this);
        m_state = ProcessState::Running;

        uintptr_t next_cr3 = get_cr3();
        uintptr_t prev_cr3 = prev->get_cr3();
        if (next_cr3 != prev_cr3) {
            lcr3(next_cr3);
        }

        switch_to(&(prev->m_context), &(m_context));
    }
}

void TaskStruct::wakeup() {
    assert(m_state != ProcessState::Zombie);
    if (m_state != ProcessState::Runnable) {
        m_state = ProcessState::Runnable;
    }
}

uintptr_t TaskStruct::get_cr3() {
    assert(m_memory != nullptr && m_memory->pgdir != nullptr);
    return P_ADDR((uintptr_t)m_memory->pgdir);
}

void TaskStruct::copy_mm(uint32_t clone_flags) {
    // TODO Full copy implementation
    m_memory = TaskManager::get_current()->m_memory;
}

void TaskStruct::copy_thread(uintptr_t esp, TrapFrame *trapFrame) {
    m_trap_frame = (TrapFrame*)(m_kernel_stack + KSTACK_SIZE) - 1;
    
    *m_trap_frame = *trapFrame;
    m_trap_frame->m_regs.m_rax = 0;  // Return value for child
    if (esp != 0) {
        m_trap_frame->m_rsp = esp;
    } else {
        // Kernel thread: use the kernel stack (just below the trap frame)
        m_trap_frame->m_rsp = (uintptr_t)m_trap_frame;
    }
    m_trap_frame->m_rflags |= 0x200;   // Enable interrupts
    m_trap_frame->m_ss = KERNEL_DS;    // Always set SS for iretq
    
    // Set up context for context switch
    m_context.rip = (uintptr_t)forkret;
    m_context.rsp = (uintptr_t)(m_trap_frame);
}

int TaskStruct::setup_kernel_stack() {
    Page *page = alloc_page();
    if (!page) {
        return -1;
    }

    m_kernel_stack = (uintptr_t)page2kva(page);
    return 0;
}

// Set process relationships (parent-child)
void TaskStruct::set_links() {
    TaskManager::add_process(this);
    if (m_parent) {
        m_parent->m_child_list.add(m_child_node);
    }
}

void TaskStruct::remove_links() {
    TaskManager::remove_process(this);
    m_child_node.unlink();
}

void TaskStruct::destroy() {
    if (m_kernel_stack != (uintptr_t)user_stack) {
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
    get_hash_node(proc->m_pid).add(proc->m_hash_node);
    s_proc_list.add(proc->m_list_node);
    nr_process++;
}

void TaskManager::remove_process(TaskStruct* proc) {
    proc->m_hash_node.unlink();
    proc->m_list_node.unlink();
    nr_process--;
}

TaskStruct *TaskManager::find_proc(int pid) {
    if (pid <= 0) {
        return nullptr;
    }
    ListNode* head = &get_hash_node(pid);
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct *proc = TaskStruct::from_hash_link(le);
        if (proc->m_pid == pid) {
            return proc;
        }
    }
    return nullptr;
}

void TaskManager::print() {
    // Print header (similar to ps aux format)
    cprintf("PID  STAT  PPID  KSTACK    MM        NAME\n");
    cprintf("---  ----  ----  --------  --------  ----------------\n");

    ListNode *le = &s_proc_list;
    while ((le = le->get_prev()) != &s_proc_list) {
        TaskStruct *proc = TaskStruct::from_list_link(le);

        cprintf("%c%-3d %-4s  %-4d  %016lx  %016lx  %s\n",
               (proc == s_current) ? '*' : ' ',
               proc->m_pid,
               state_str(proc->m_state),
               (proc->m_parent ? proc->m_parent->m_pid : -1),
               proc->m_kernel_stack,
               (uintptr_t)proc->m_memory,
               proc->name);
    }
    
    cprintf("\nTotal processes: %d\n", nr_process);
    cprintf("Current process: %s (PID %d)\n", s_current->name, s_current->m_pid);
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
    InterruptsGuard guard;

    TaskStruct *next = s_idle_proc;
    ListNode* head = &s_proc_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct *proc = TaskStruct::from_list_link(le);
        if (proc->m_state == ProcessState::Runnable) {
            next = proc;
            break;
        }
    }
    
    if (next != s_current) {
        if (s_current->m_state == ProcessState::Running) {
            s_current->m_state = ProcessState::Runnable; 
        }
        next->run();
    }
}

int TaskManager::fork(uint32_t cloneFlags, uintptr_t stack, TrapFrame *trapFrame) {
    TaskStruct *proc = new TaskStruct();
    proc->m_parent = get_current();
    proc->m_child_list.init();
    
    proc->setup_kernel_stack();
    proc->copy_mm(cloneFlags);
    proc->copy_thread(stack, trapFrame);

    {
        InterruptsGuard guard;
        proc->m_pid = get_pid();
        proc->set_links();
    }

    proc->wakeup();
    return proc->m_pid;
}

int TaskManager::exit(int errorCode) {
    TaskStruct* current = get_current();
    current->m_state = ProcessState::Zombie;
    current->m_exit_code = errorCode;

    {
        InterruptsGuard guard;

        if (current->m_parent && current->m_parent->m_wait_state) {
            current->m_parent->wakeup();
        }

        ListNode* children = &current->m_child_list;
        while (!children->empty()) {
            ListNode* node = children->get_next();
            node->unlink();
            
            TaskStruct* child = TaskStruct::from_child_link(node);
            child->m_parent = s_init_proc;
            s_init_proc->m_child_list.add(*node);
            
            // If child is zombie, it was waiting to be reaped by its original parent.
            // Now that init is the new parent, wake up init so it can reap the zombie.
            // This ensures zombie processes don't linger indefinitely after reparenting.
            if (child->m_state == ProcessState::Zombie) {
                if (s_init_proc->m_wait_state) {
                    s_init_proc->wakeup();
                }
            }
        }
    }

    schedule();
    panic("TaskManager::exit will not return!");
    return 0;  // Never reached
}

int TaskManager::wait(int pid, int* codeStore) {
    TaskStruct* current = get_current();
    
    while (true) {
        bool hasChildren = false;
        TaskStruct* zombieChild = nullptr;
        
        {
            InterruptsGuard guard;
            
            ListNode* head = &current->m_child_list;
            for (auto* le = head->get_next(); le != head; le = le->get_next()) {
                TaskStruct* child = TaskStruct::from_child_link(le);
                hasChildren = true;

                if (pid == 0 || child->m_pid == pid) {
                    if (child->m_state == ProcessState::Zombie) {
                        zombieChild = child;
                        break;
                    }
                }
            }
        }
        
        if (zombieChild) {
            int childPid = zombieChild->m_pid;
            if (codeStore) {
                *codeStore = zombieChild->m_exit_code;
            }
            
            {
                InterruptsGuard guard;
                zombieChild->remove_links();
            }
            zombieChild->destroy();
            
            return childPid;
        }
        
        if (!hasChildren) {
            return -1;
        }

        current->m_wait_state = 1;
        current->m_state = ProcessState::Sleeping;
        schedule();
        current->m_wait_state = 0;
    }
}

// Initialize idle process (PID 0)
void TaskManager::init_idle() {
    TaskStruct* idleProc = new TaskStruct();
    idleProc->m_state = ProcessState::Runnable;
    idleProc->m_kernel_stack = (uintptr_t)user_stack;  // Use boot stack
    idleProc->m_child_list.init();
    
    // Idle process uses kernel's init_mm (shared by all kernel threads)
    idleProc->m_memory = &init_mm;
    
    strcpy(idleProc->name, "idle");

    s_idle_proc = idleProc;
    set_current(idleProc);
    add_process(idleProc);
}

// Create init process using fork (PID 1)
void TaskManager::init_init_proc() {
    TrapFrame trapFrame {};

    trapFrame.m_cs = KERNEL_CS;
    trapFrame.m_rflags = FL_IF;  // Enable interrupts
    trapFrame.m_rip = (uintptr_t)init_main;
    trapFrame.m_rsp = 0;  // Will be set up by copy_thread

    int ret = fork(0, 0, &trapFrame);
    s_init_proc = find_proc(ret);

    strcpy(s_init_proc->name, "init");
}

namespace sched {

void init() {
    TaskManager::init();
}

} // namespace sched
