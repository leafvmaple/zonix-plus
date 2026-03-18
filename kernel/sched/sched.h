#pragma once

#include <asm/context.h>
#include <base/types.h>

#include "lib/list.h"
#include "trap/trap.h"
#include "mm/vmm.h"

// Process states - modeling Linux's approach
enum class ProcessState : uint8_t {
    Uninit = 0,    // uninitialized
    Sleeping = 1,  // sleeping (blocked, waiting for event)
    Runnable = 2,  // runnable (might be in run queue)
    Running = 3,   // running
    Zombie = 4,    // almost dead (waiting to be cleaned up)
};

// Priority levels (lower value = higher priority)
namespace sched_prio {
inline constexpr int MAX_PRIO = 0;    // Highest priority
inline constexpr int DEFAULT = 10;    // Normal processes
inline constexpr int MIN_PRIO = 20;   // Lowest priority
inline constexpr int IDLE_PRIO = 31;  // Idle process only

// Base timeslice in ticks (10ms each). Higher priority = more ticks.
inline constexpr int BASE_TIMESLICE = 10;  // 100ms default
}  // namespace sched_prio

// Process control block - modeling Linux's task_struct
struct TaskStruct {
    static constexpr size_t KSTACK_SIZE = 4096;  // 4KB kernel stack

    char name[32]{};  // Process name
    int pid{};        // Process ID

    volatile ProcessState state{};  // Process state
    uintptr_t kernel_stack{};       // Kernel stack bottom
    MemoryDesc* memory{};           // Memory management
    Context context{};              // Process context for switching
    TrapFrame* trap_frame{};        // Trap frame for current interrupt
    uint32_t flags{};               // Process flags

    ListNode list_node{};   // Link in process list
    ListNode hash_node{};   // Link in hash list
    ListNode child_node{};  // Link in parent's child list
    int exit_code{};        // Exit code (for zombie processes)
    uint32_t wait_state{};  // Waiting state

    TaskStruct* parent{};   // Parent process
    ListNode child_list{};  // Head of child process list

    // Scheduling fields
    int priority{sched_prio::DEFAULT};  // Static priority (lower = higher)
    int time_slice{};                   // Remaining ticks in current quantum
    volatile int need_resched{};        // Set by timer ISR to request reschedule

    void run();
    void wakeup();

    [[nodiscard]] uintptr_t get_cr3() const;
    void copy_mm(uint32_t clone_flags);
    void copy_thread(uintptr_t esp, TrapFrame* src_tf);
    int setup_kernel_stack();

    ListNode* node() { return &list_node; }

    static TaskStruct* from_list_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(reinterpret_cast<char*>(node) - offset_of(&TaskStruct::list_node));
    }

    static TaskStruct* from_hash_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(reinterpret_cast<char*>(node) - offset_of(&TaskStruct::hash_node));
    }

    static TaskStruct* from_child_link(ListNode* node) {
        return reinterpret_cast<TaskStruct*>(reinterpret_cast<char*>(node) - offset_of(&TaskStruct::child_node));
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

    static ListNode s_proc_list;                  // All processes list (init in cpp)
    static ListNode s_hash_list[HASH_LIST_SIZE];  // Hash table (init in cpp)

    inline static TaskStruct* s_idle_proc{};  // Idle process (PID 0)
    inline static TaskStruct* s_init_proc{};  // Init process (PID 1)
    inline static int nr_process{};           // Number of processes

    static int init();

    static inline ListNode& get_hash_node(int pid) { return s_hash_list[pid_hash(pid)]; }

    static inline TaskStruct* get_current() { return s_current; }

    static inline void set_current(TaskStruct* proc) { s_current = proc; }

    static void add_process(TaskStruct* proc);
    static void remove_process(TaskStruct* proc);

    static TaskStruct* find_proc(int pid);

    static void print();

    // Scheduling and process lifecycle
    static void schedule();
    static void tick();  // Called from timer ISR each tick
    static int fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame);
    static int kernel_thread(int (*fn)(void*), void* arg);
    static int exit(int error_code);
    static int wait(int pid, int* code_store);

    // Compute timeslice from priority
    static int calc_timeslice(int priority);

private:
    inline static TaskStruct* s_current{};
    inline static ListNode* s_sched_cursor{};  // Round-robin cursor

    static uint32_t pid_hash(int x);

    // Initialization helpers
    static void init_idle();
    static void init_init_proc();
};

namespace sched {

int init();

// Scheduling and process lifecycle
void schedule();
void tick();  // Called from timer ISR each tick
int fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf);
int kernel_thread(int (*fn)(void*), void* arg);
int exit(int error_code);
int wait(int pid, int* code_store);

// Query
TaskStruct* current();
TaskStruct* find_proc(int pid);
void print();

}  // namespace sched