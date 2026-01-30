#pragma once

#include <arch/x86/segments.h>
#include <base/types.h>

#include "../include/list.h"
#include "../trap/trap.h"
#include "../mm/vmm.h"

namespace sched {

inline constexpr int FIRST_TSS_ENTRY = 4;
inline constexpr size_t KSTACK_SIZE  = 4096;  // 4KB kernel stack

} // namespace sched

// Legacy compatibility
#define FIRST_TSS_ENTRY sched::FIRST_TSS_ENTRY
#define KSTACK_SIZE     sched::KSTACK_SIZE

// Process states - modeling Linux's approach
enum class ProcessState : int {
    Uninit   = 0,     // uninitialized
    Sleeping = 1,     // sleeping (blocked, waiting for event)
    Runnable = 2,     // runnable (might be in run queue)
    Running  = 3,     // running
    Zombie   = 4,     // almost dead (waiting to be cleaned up)
};

// Legacy compatibility
using proc_state = ProcessState;
constexpr auto TASK_UNINIT   = ProcessState::Uninit;
constexpr auto TASK_SLEEPING = ProcessState::Sleeping;
constexpr auto TASK_RUNNABLE = ProcessState::Runnable;
constexpr auto TASK_RUNNING  = ProcessState::Running;
constexpr auto TASK_ZOMBIE   = ProcessState::Zombie;

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
struct TaskStruct {
    volatile ProcessState state;       // Process state
    int pid;                           // Process ID
    uintptr_t kstack;                  // Kernel stack bottom
    TaskStruct* parent;                // Parent process
    mm_struct* mm;                     // Memory management
    struct context context;            // Process context for switching
    TrapFrame* tf;                    // Trap frame for current interrupt
    uint32_t flags;                    // Process flags
    char name[32];                     // Process name
    ListNode list_link;                // Link in process list
    ListNode hash_link;                // Link in hash list
    int exit_code;                     // Exit code (for zombie processes)
    uint32_t wait_state;               // Waiting state
    TaskStruct* cptr;                  // child pointer
    TaskStruct* yptr;                  // younger sibling
    TaskStruct* optr;                  // older sibling
};

using task_struct = TaskStruct;

// Macros for process management
#define le2proc(le, member) \
    TO_STRUCT(le, TaskStruct, member)

// Global functions
void sched_init();
void schedule();
void wakeup_proc(TaskStruct* proc);
int do_fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf);
int do_exit(int error_code);
int do_wait(int pid, int* code_store);

// Get current running process
extern TaskStruct* current;
TaskStruct* get_current();

inline void set_current(TaskStruct* proc) { current = proc; }

// Get process CR3 (page directory physical address)
uintptr_t proc_get_cr3(TaskStruct* proc);

// Print all processes information (for ps command)
void print_all_procs();