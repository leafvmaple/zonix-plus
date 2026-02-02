#include "sched.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../include/stdio.h"
#include "../include/memory.h"
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
extern mm_struct init_mm;  // Global kernel mm_struct

// Global process management variables
static ListNode proc_list{};              // All processes list
static task_struct *idle_proc{}; // Idle process (PID 0)
static task_struct *init_proc{}; // Init process (PID 1)
task_struct *current{};          // Current running process

static int nr_process = 0;            // Number of processes

// Process hash table for fast PID lookup
#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

static ListNode hash_list[HASH_LIST_SIZE];

// Simple hash function
static inline uint32_t hash32(uint32_t val, unsigned int bits) {
    uint32_t hash = val * 0x61C88647;
    return hash >> (32 - bits);
}

// Get current process
task_struct *get_current(void) {
    return current;
}

// Get process CR3 (page directory physical address)
uintptr_t proc_get_cr3(task_struct *proc) {
    // All processes (including kernel threads) should have mm
    assert(proc->mm != nullptr && proc->mm->pgdir != nullptr);
    return P_ADDR((uintptr_t)proc->mm->pgdir);
}

// Allocate a new process structure
static task_struct *alloc_proc(void) {
    task_struct *proc = (task_struct *)kmalloc(sizeof(task_struct));
    if (proc) {
        proc->state = TASK_UNINIT;
        proc->pid = -1;
        proc->kstack = 0;
        proc->parent = nullptr;
        proc->mm = nullptr;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = nullptr;
        proc->flags = 0;
        memset(proc->name, 0, sizeof(proc->name));
        proc->wait_state = 0;
        proc->cptr = proc->optr = proc->yptr = nullptr;
    }
    return proc;
}

// Set up kernel stack for process
static int setup_kstack(task_struct *proc) {
    Page *page = alloc_page();
    if (page) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -1;
}

// Free kernel stack
static void free_kstack(task_struct *proc) {
    free_page(kva2page((void *)proc->kstack));
}

// Copy memory management structure
static int copy_mm(uint32_t clone_flags, task_struct *proc) {
    // If parent has no user mm (kernel thread), use init_mm
    if (current->mm == &init_mm) {
        proc->mm = &init_mm;  // Kernel thread inherits kernel mm
        return 0;
    }
    
    // User process: create new mm_struct (will implement later)
    // For now, just share the parent's mm
    // clone_flags & 0x00000100
    proc->mm = current->mm;
    return 0;
}

// Forward declaration
extern "C" void forkret(void);
extern void trapret(void);

// Copy process thread state
static void copy_thread(task_struct *proc, uintptr_t esp, TrapFrame *tf) {
    proc->tf = (TrapFrame *)(proc->kstack + KSTACK_SIZE) - 1;
    
    *proc->tf = *tf;
    proc->tf->m_regs.m_eax = 0;  // Return value for child
    proc->tf->m_esp = esp;
    proc->tf->m_eflags |= 0x200;   // Enable interrupts
    
    // Set up context for context switch
    proc->context.eip = (uintptr_t)forkret;
    proc->context.esp = (uintptr_t)(proc->tf);
}

// Allocate a unique PID
static int get_pid(void) {
    static int next_pid = 1;

    return next_pid++;
}

// Add process to hash list and proc_list
static void hash_proc(task_struct *proc) {
    (hash_list + pid_hashfn(proc->pid))->add(proc->hash_link);
}

static void unhash_proc(task_struct *proc) {
    proc->hash_link.del();
}

// Find process by PID using hash table
static task_struct *find_proc(int pid) {
    if (pid <= 0) {
        return nullptr;
    }
    ListNode *list = hash_list + pid_hashfn(pid), *le = list;
    while ((le = le->get_next()) != list) {
        task_struct *proc = le2proc(le, hash_link);
        if (proc->pid == pid) {
            return proc;
        }
    }
    return nullptr;
}

// Set process relationships (parent-child)
static void set_links(task_struct *proc) {
    proc_list.add(proc->list_link);
    proc->yptr = nullptr;
    
    if ((proc->optr = proc->parent->cptr) != nullptr) {
        proc->optr->yptr = proc;
    }
    
    proc->parent->cptr = proc;
    nr_process++;
}

static void remove_links(task_struct *proc) {
    proc->list_link.del();
    
    if (proc->optr != nullptr) {
        proc->optr->yptr = proc->yptr;
    }
    
    if (proc->yptr != nullptr) {
        proc->yptr->optr = proc->optr;
    } else {
        proc->parent->cptr = proc->optr;
    }
    
    nr_process--;
}

// Wake up a sleeping process
void wakeup_proc(task_struct *proc) {
    assert(proc->state != TASK_ZOMBIE);
    
    if (proc->state != TASK_RUNNABLE) {
        proc->state = TASK_RUNNABLE;
    }
}

// Forward declaration
void proc_run(task_struct *proc);

// Simple round-robin scheduler
void schedule(void) {
    InterruptsGuard guard;
    
    if (current->state == TASK_RUNNING) {
        current->state = TASK_RUNNABLE; 
    }
    
    // Find next runnable process
    task_struct *next = idle_proc;
    ListNode *le = &proc_list;
    while ((le = le->get_next()) != &proc_list) {
        task_struct *proc = le2proc(le, list_link);
        if (proc->state == TASK_RUNNABLE) {
            next = proc;
            break;
        }
    }
    
    if (next != current) {
        proc_run(next);
    }
}

// Context switch wrapper (will be implemented in assembly)
extern "C" void switch_to(struct context *from, struct context *to);

// Switch to a process
void proc_run(task_struct *proc) {
    if (proc != current) {
        InterruptsGuard guard;
        
        task_struct *prev = current, *next = proc;
        
        // Update current BEFORE switching (critical!)
        current = next;
        next->state = TASK_RUNNING;
        
        // Switch page directory if needed
        uintptr_t next_cr3 = proc_get_cr3(next);
        uintptr_t prev_cr3 = proc_get_cr3(prev);
        if (next_cr3 != prev_cr3) {
            lcr3(next_cr3);
        }
        
        // Switch context - after this, we're in the new process
        // When switch_to returns, we are already in 'next' process
        switch_to(&(prev->context), &(next->context));
    }
}

// Do fork system call
int do_fork(uint32_t clone_flags, uintptr_t stack, TrapFrame *tf) {
    // Allocate process structure
    task_struct *proc = alloc_proc();
    proc->parent = current;
    
    setup_kstack(proc);
    copy_mm(clone_flags, proc);
    copy_thread(proc, stack, tf);

    {
        InterruptsGuard guard;
        proc->pid = get_pid();
        hash_proc(proc);
        set_links(proc);
    }
    
    // Wake up the process
    wakeup_proc(proc);
    
    return proc->pid;
}

// Do exit system call
int do_exit(int error_code) {
    current->state = TASK_ZOMBIE;
    current->exit_code = error_code;

    // Free mm if not shared
    // (Will implement later)
    {
        InterruptsGuard guard;
        // Wake up parent if waiting
        if (current->parent && current->parent->wait_state) {
            wakeup_proc(current->parent);
        }
        
        // Give children to init process if it exists
        if (init_proc != nullptr) {
            while (current->cptr != nullptr) {
                task_struct *proc = current->cptr;
                current->cptr = proc->optr;
                
                proc->yptr = nullptr;
                if ((proc->optr = init_proc->cptr) != nullptr) {
                    init_proc->cptr->yptr = proc;
                }
                proc->parent = init_proc;
                init_proc->cptr = proc;
                
                if (proc->state == TASK_ZOMBIE) {
                    if (init_proc->wait_state) {
                        wakeup_proc(init_proc);
                    }
                }
            }
        }
    }

    schedule();
    panic("do_exit will not return!");
    return 0;  // Never reached
}

// Initialize idle process (PID 0)
static void idle_init(void) {
    idle_proc = alloc_proc();
    idle_proc->pid = 0;
    idle_proc->state = TASK_RUNNABLE;
    idle_proc->kstack = (uintptr_t)user_stack;  // Use boot stack
    
    // Idle process uses kernel's init_mm (shared by all kernel threads)
    idle_proc->mm = &init_mm;
    
    memcpy(idle_proc->name, "idle", 5);
    
    nr_process++;
    
    // Set current to idle (required for do_fork)
    current = idle_proc;
    
    // Add to hash and list
    hash_proc(idle_proc);
    proc_list.add(idle_proc->list_link);
}

// Init process main function (kernel thread entry point)
static int init_main(void *arg) {
    // System is now fully initialized, show prompt
    shell_prompt();
    
    // Init's main loop
    while (1) {
        schedule();
    }
    
    panic("init process exited!");
    return 0;
}

// Create init process using do_fork (PID 1)
static int init_proc_init(void) {
    int ret;
    
    // Create a fake trap frame for fork
    // Since we're creating a kernel thread, we need minimal setup
    TrapFrame tf;
    memset(&tf, 0, sizeof(TrapFrame));
    
    // Set up for kernel thread execution
    tf.m_cs = KERNEL_CS;
    tf.m_eflags = FL_IF;  // Enable interrupts
    tf.m_eip = (uintptr_t)init_main;
    tf.m_esp = 0;  // Will be set up by copy_thread
    
    // Fork to create init process
    // current is idle at this point
    ret = do_fork(0, 0, &tf);
    init_proc = find_proc(ret);
    memcpy(init_proc->name, "init", 5);
    
    cprintf("init process created via do_fork (PID %d)\n", init_proc->pid);
    return ret;
}

// Get process state string
static const char *state_str(ProcessState state) {
    switch (state) {
        case TASK_UNINIT:    return "U";  // Uninitialized
        case TASK_SLEEPING:  return "S";  // Sleeping
        case TASK_RUNNABLE:  return "R";  // Runnable
        case TASK_RUNNING:   return "R+"; // Running (with +)
        case TASK_ZOMBIE:    return "Z";  // Zombie
        default:             return "?";  // Unknown
    }
}

// Print all processes information (like Linux ps command)
void print_all_procs(void) {
    // Print header (similar to ps aux format)
    cprintf("PID  STAT  PPID  KSTACK    MM        NAME\n");
    cprintf("---  ----  ----  --------  --------  ----------------\n");
    
    ListNode *le = &proc_list;
    while ((le = le->get_prev()) != &proc_list) {
        task_struct *proc = le2proc(le, list_link);
        
        // Mark current process
        char mark = (proc == current) ? '*' : ' ';
        
        cprintf("%c%-3d %-4s  %-4d  %08x  %08x  %s\n",
               mark,
               proc->pid,
               state_str(proc->state),
               (proc->parent ? proc->parent->pid : -1),
               proc->kstack,
               proc->mm,
               proc->name);
    }
    
    cprintf("\nTotal processes: %d\n", nr_process);
    cprintf("Current process: %s (PID %d)\n", current->name, current->pid);
}

// Initialize process management
void sched_init(void) {
    // Initialize process list and hash table
    proc_list.init();
    for (int i = 0; i < HASH_LIST_SIZE; i++) {
        (hash_list + i)->init();
    }
    
    idle_init();
    init_proc_init();
    
    cprintf("sched init: idle process PID = 0, init process PID = 1\n");
}