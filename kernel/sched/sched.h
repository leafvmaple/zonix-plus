#pragma once

#include <asm/segments.h>
#include <base/types.h>

#include "../include/list.h"
#include "../trap/trap.h"
#include "../mm/vmm.h"

// Process states - modeling Linux's approach
enum class ProcessState : int {
    Uninit   = 0,     // uninitialized
    Sleeping = 1,     // sleeping (blocked, waiting for event)
    Runnable = 2,     // runnable (might be in run queue)
    Running  = 3,     // running
    Zombie   = 4,     // almost dead (waiting to be cleaned up)
};

// Context for process switching (x86_64 callee-saved registers)
struct Context {
    uint64_t rip{};
    uint64_t rsp{};
    uint64_t rbx{};
    uint64_t rbp{};
    uint64_t r12{};
    uint64_t r13{};
    uint64_t r14{};
    uint64_t r15{};
};

// Process control block - modeling Linux's task_struct
struct TaskStruct {    
    static constexpr size_t KSTACK_SIZE  = 4096;  // 4KB kernel stack

    char name[32]{};                     // Process name
    int m_pid{};                         // Process ID

    volatile ProcessState m_state{};     // Process state
    uintptr_t m_kernel_stack{};          // Kernel stack bottom
    MemoryDesc* m_memory{};              // Memory management
    Context m_context{};                 // Process context for switching
    TrapFrame* m_trap_frame{};           // Trap frame for current interrupt
    uint32_t m_flags{};                  // Process flags

    ListNode m_list_node{};              // Link in process list
    ListNode m_hash_node{};              // Link in hash list
    ListNode m_child_node{};             // Link in parent's child list
    int m_exit_code{};                   // Exit code (for zombie processes)
    uint32_t m_wait_state{};             // Waiting state

    TaskStruct* m_parent{};              // Parent process
    ListNode m_child_list{};             // Head of child process list
    
    void run();
    void wakeup();

    uintptr_t get_cr3();
    void copy_mm(uint32_t clone_flags);
    void copy_thread(uintptr_t esp, TrapFrame *tf);
    int setup_kernel_stack();
    
    ListNode* node() {
        return &m_list_node;
    }

    static TaskStruct* from_list_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(
            reinterpret_cast<char*>(node) - offset_of(&TaskStruct::m_list_node));
    }

    static TaskStruct* from_hash_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(
            reinterpret_cast<char*>(node) - offset_of(&TaskStruct::m_hash_node));
    }

    static TaskStruct* from_child_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(
            reinterpret_cast<char*>(node) - offset_of(&TaskStruct::m_child_node));
    }

    void set_links();
    void remove_links();
    void destroy();

    friend class TaskManager;
};

class TaskManager {
public:
    static constexpr int HASH_SHIFT = 10;
    static constexpr size_t HASH_LIST_SIZE = 1 << HASH_SHIFT;

    static ListNode s_proc_list;                 // All processes list (init in cpp)
    static ListNode s_hash_list[HASH_LIST_SIZE]; // Hash table (init in cpp)

    inline static TaskStruct* s_idle_proc{};     // Idle process (PID 0)
    inline static TaskStruct* s_init_proc{};     // Init process (PID 1)
    inline static int nr_process{};              // Number of processes

    static void init();

    static inline ListNode& get_hash_node(int pid) {
        return s_hash_list[pid_hash(pid)];
    }

    static inline TaskStruct* get_current() {
        return s_current;
    }

    static inline void set_current(TaskStruct* proc) {
        s_current = proc;
    }

    static void add_process(TaskStruct* proc);
    static void remove_process(TaskStruct* proc);

    static TaskStruct* find_proc(int pid);

    static void print();

    // Scheduling and process lifecycle
    static void schedule();
    static int fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf);
    static int exit(int error_code);
    static int wait(int pid, int* code_store);

private:
    inline static TaskStruct* s_current{};

    static uint32_t pid_hash(int x);

    // Initialization helpers
    static void init_idle();
    static void init_init_proc();
};

namespace sched {

void init();
void test();

} // namespace sched