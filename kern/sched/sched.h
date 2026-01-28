#pragma once

#include <arch/x86/segments.h>
#include <base/types.h>

#include "../include/list.h"
#include "../trap/trap.h"
#include "../mm/vmm.h"

#define FIRST_TSS_ENTRY 4
#define KSTACK_SIZE 4096  // 4KB kernel stack

// Process states - modeling Linux's approach
enum proc_state {
    TASK_UNINIT = 0,     // uninitialized
    TASK_SLEEPING,       // sleeping (blocked, waiting for event)
    TASK_RUNNABLE,       // runnable (might be in run queue)
    TASK_RUNNING,        // running
    TASK_ZOMBIE,         // almost dead (waiting to be cleaned up)
};

// Context for process switching
struct context {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
};

// Process control block - modeling Linux's task_struct
typedef struct task_struct {
    volatile enum proc_state state;    // Process state
    int pid;                           // Process ID
    uintptr_t kstack;                  // Kernel stack bottom
    struct task_struct *parent;        // Parent process
    mm_struct *mm;                     // Memory management
    struct context context;            // Process context for switching
    trap_frame *tf;                    // Trap frame for current interrupt
    uint32_t flags;                    // Process flags
    char name[32];                     // Process name
    list_entry_t list_link;            // Link in process list
    list_entry_t hash_link;            // Link in hash list
    int exit_code;                     // Exit code (for zombie processes)
    uint32_t wait_state;               // Waiting state
    struct task_struct *cptr, *yptr, *optr;   // child/younger/older sibling
} task_struct;

// Macros for process management
#define le2proc(le, member) \
    ((task_struct *)((char *)(le) - offsetof(task_struct, member)))

#define offsetof(type, member) \
    ((size_t)(&((type *)0)->member))

// Global functions
void sched_init(void);
void schedule(void);
void wakeup_proc(task_struct *proc);
int do_fork(uint32_t clone_flags, uintptr_t stack, trap_frame *tf);
int do_exit(int error_code);
int do_wait(int pid, int *code_store);

// Get current running process
extern task_struct *current;
task_struct *get_current(void);

#define set_current(proc) do { current = (proc); } while (0)

// Get process CR3 (page directory physical address)
uintptr_t proc_get_cr3(task_struct *proc);

// Print all processes information (for ps command)
void print_all_procs(void);