#pragma once

#include <asm/context.h>
#include <base/types.h>

#include "lib/list.h"
#include "lib/result.h"
#include "lib/kernel_buffer.h"
#include "fs/fd.h"
#include "mm/vmm.h"
#include "trap/trap.h"

namespace vfs {
class File;
}

enum class TaskState : uint8_t {
    Uninit = 0,    // uninitialized
    Sleeping = 1,  // sleeping (blocked, waiting for event)
    Runnable = 2,  // runnable (might be in run queue)
    Running = 3,   // running
    Zombie = 4,    // almost dead (waiting to be cleaned up)
};

namespace sched_prio {
inline constexpr int HIGHEST_PRIORITY = 0;  // Highest priority
inline constexpr int DEFAULT = 10;          // Normal processes
inline constexpr int LOWEST_PRIORITY = 20;  // Lowest priority
inline constexpr int IDLE_PRIO = 31;        // Idle process only

inline constexpr int BASE_TIMESLICE = 10;  // 100ms default
}  // namespace sched_prio

struct Task;

class SchedulerPolicy {
public:
    [[nodiscard]] const char* name() const;
    [[nodiscard]] int calc_time_slice(int priority) const;
    void tick(Task* current, Task* idle) const;
    [[nodiscard]] Task* pick_next(ListNode& proc_list, Task* idle, Task* current = nullptr);
    void task_removed(const Task& task);

private:
    ListNode* cursor_{};
};

// Process control block - modeling Linux's task_struct
struct Task {
    static constexpr size_t KSTACK_SIZE = 4096;  // 4KB kernel stack

    Task() = default;
    ~Task();
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

    int pid{};  // Process ID

private:
    char name_[32]{};            // Process name
    Context context_{};          // Process context for switching
    KernelBuffer kernel_stack_;  // Owned dynamic stack
    uintptr_t boot_stack_{};     // Borrowed permanent boot stack
    sys::unique_ptr<MemoryDesc> owned_memory_;
    MemoryDesc* borrowed_memory_{};
    volatile TaskState state_{};  // Process state
    fd::Table files_{};
    friend struct TaskAccess;

public:
    TrapFrame* trap_frame{};  // Trap frame for current interrupt
    uint32_t flags{};         // Process flags

    ListNode list_node{};   // Link in process list
    ListNode hash_node{};   // Link in hash list
    ListNode child_node{};  // Link in parent's child list
    int exit_code{};        // Exit code (for zombie processes)
    uint32_t wait_state{};  // Waiting state

    Task* parent{};         // Parent process
    ListNode child_list{};  // Head of child process list

    // Scheduling fields
    int priority{sched_prio::DEFAULT};  // Static priority (lower = higher)
    int time_slice{};                   // Remaining ticks in current quantum
    volatile int need_resched{};        // Set by timer ISR to request reschedule

    void run();
    void sleep();
    void wakeup();
    void mark_running();
    void mark_zombie(int code);
    void set_name(const char* name);
    [[nodiscard]] TaskState state() const { return state_; }
    [[nodiscard]] uintptr_t page_table_root_pa() const;

    [[nodiscard]] MemoryDesc* memory() const { return owned_memory_ ? owned_memory_.get() : borrowed_memory_; }
    void use_kernel_memory() {
        assert(!owned_memory_ && state_ == TaskState::Uninit);
        borrowed_memory_ = &vmm::Manager::kernel_mm();
    }
    // Only an unpublished or non-running child may receive a prepared address space.
    void adopt_memory(sys::unique_ptr<MemoryDesc> memory);
    [[nodiscard]] uintptr_t kernel_stack() const {
        return kernel_stack_.empty() ? boot_stack_ : reinterpret_cast<uintptr_t>(kernel_stack_.data());
    }
    void copy_thread(uintptr_t stack_pointer, TrapFrame* src_tf);
    Error setup_kernel_stack();
    [[nodiscard]] fd::Table& files() { return files_; }
    [[nodiscard]] const fd::Table& files() const { return files_; }

    ListNode* node() { return &list_node; }

    static Task* from_list_link(ListNode* node) {
        return reinterpret_cast<Task*>(reinterpret_cast<char*>(node) - offset_of(&Task::list_node));
    }

    static Task* from_hash_link(ListNode* node) {
        return reinterpret_cast<Task*>(reinterpret_cast<char*>(node) - offset_of(&Task::hash_node));
    }

    static Task* from_child_link(ListNode* node) {
        return reinterpret_cast<Task*>(reinterpret_cast<char*>(node) - offset_of(&Task::child_node));
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

    static int init();

    static ListNode& task_list() { return proc_list_; }
    static Task* idle_task() { return idle_proc_; }
    static Task* init_task() { return init_proc_; }
    static int process_count() { return process_count_; }

    static inline ListNode& hash_node(int pid) { return hash_list_[pid_hash(pid)]; }

    static inline Task* current() { return current_; }
    static inline void set_current(Task* proc) { current_ = proc; }

    static void add_process(Task* proc);
    static void remove_process(Task* proc);

    static Task* find_process(int pid);

    static void print();
    static void print_stats();

    // Scheduling and process lifecycle
    static void schedule();
    static void tick();  // Called from timer ISR each tick
    static Result<int> fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame);
    static Result<int> kernel_thread(int (*fn)(void*), void* arg);
    static int exit(int error_code);
    static Result<int> wait(int pid, int* code_store);

private:
    friend struct Task;
    inline static ListNode proc_list_{};
    inline static ListNode hash_list_[HASH_LIST_SIZE]{};
    inline static Task* idle_proc_{};  // Idle process (PID 0)
    inline static Task* init_proc_{};  // Init process (PID 1)
    inline static int process_count_{};
    inline static Task* current_{};
    inline static SchedulerPolicy policy_{};
    inline static int next_pid_ = 1;

    // Scheduler telemetry counters (monotonic since boot)
    inline static uint64_t tick_count_{};
    inline static uint64_t schedule_calls_{};
    inline static uint64_t need_resched_events_{};
    inline static uint64_t context_switches_{};
    inline static uint64_t same_task_runs_{};
    inline static uint64_t pick_idle_{};
    inline static uint64_t pick_non_idle_{};

    static uint32_t pid_hash(int x);

    // Initialization helpers
    static int init_idle();
    static int init_init_proc();
};

inline void Task::adopt_memory(sys::unique_ptr<MemoryDesc> memory) {
    assert(memory && !owned_memory_ && this != TaskManager::current());
    assert(state_ == TaskState::Uninit || state_ == TaskState::Runnable);
    owned_memory_ = sys::move(memory);
    borrowed_memory_ = nullptr;
}

inline Task::~Task() {
    // Published tasks must be off CPU and detached before resource release.
    assert(this != TaskManager::current());
    assert(list_node.empty() && hash_node.empty() && child_node.empty() && child_list.empty());
    files_.close_all();
    kernel_stack_ = KernelBuffer{};
    // owned_memory_ releases the address space after files and stack; borrows are untouched.
}

namespace sched {

int init();

void schedule();
void tick();  // Called from timer ISR each tick
Result<int> fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf);
Result<int> kernel_thread(int (*fn)(void*), void* arg);
int exit(int error_code);
Result<int> wait(int pid, int* code_store);

Task* current();
Task* find_process(int pid);
void print();
void print_stats();

}  // namespace sched
