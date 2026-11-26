#pragma once

#include <asm/context.h>
#include <base/types.h>

#include <debug/assert.h>
#include <sys/intrusive_list.hpp>
#include <sys/array.hpp>
#include "lib/result.h"
#include "sched/kernel_stack.h"
#include "fs/fd.h"
#include "cons/pty.h"
#include "mm/vmm.h"
#include "trap/trap.h"

namespace vfs {
class File;
}

using KernelThreadEntry = int (*)(void*);

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

// Quantum at the default priority, in ticks (30 ms). Waking tasks of higher
// priority do not wait for it to run out.
inline constexpr int BASE_TIMESLICE = 3;
}  // namespace sched_prio

struct Task;
struct ProcessInfo;

// Process control block - modeling Linux's task_struct
struct Task {
    // One page was too small: starting a program from a pipeline on aarch64
    // ran past it. KernelStack puts a guard page below, so an overflow faults.
    static constexpr size_t KSTACK_SIZE = KernelStack::SIZE;

    Task() = default;
    ~Task();
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

    int pid{};  // Process ID

private:
    char cwd_[128]{"/"};
    char name_[32]{};             // Process name
    Context context_{};           // Process context for switching
    KernelStack kernel_stack_;    // Owned dynamic stack
    uintptr_t boot_stack_{};      // Borrowed permanent boot stack
    uintptr_t boot_stack_top_{};  // Its upper end
    sys::unique_ptr<MemoryDesc> owned_memory_;
    MemoryDesc* borrowed_memory_{};
    bool termination_requested_{};
    int console_job_{};
    pty::SessionRef pty_;
    volatile TaskState state_{};  // Process state
    fd::Table files_{};
    friend struct TaskAccess;
    friend class TaskManager;

public:
    TrapFrame* trap_frame{};  // Trap frame for current interrupt
    uint32_t flags{};         // Process flags

    sys::list_node<Task> list_node{*this};   // Link in process list
    sys::list_node<Task> hash_node{*this};   // Link in hash list
    sys::list_node<Task> child_node{*this};  // Link in parent's child list
    int exit_code{};                         // Exit code (for zombie processes)
    uint32_t wait_state{};                   // Waiting state

    Task* parent{};  // Parent process
    using ChildList = sys::intrusive_list<Task, &Task::child_node>;
    ChildList child_list{};  // Head of child process list

    // Scheduling fields
    int priority{sched_prio::DEFAULT};  // Static priority (lower = higher)
    int time_slice{};                   // Remaining ticks in current quantum
    volatile int need_resched{};        // Set by timer ISR to request reschedule

    pty::Session* pty_session() const { return pty_.get(); }
    void set_pty_session(pty::SessionRef session) {
        assert(state_ == TaskState::Uninit);
        pty_ = sys::move(session);
    }
    int console_job() const { return console_job_; }
    void set_console_job(int owner) {
        assert(state_ == TaskState::Uninit);
        console_job_ = owner;
    }
    bool termination_requested() const { return termination_requested_; }
    void run();
    void sleep();
    void wakeup();
    void mark_running();
    void mark_zombie(int code);
    void set_name(const char* name);
    [[nodiscard]] const char* name() const { return name_; }
    const char* cwd() const { return cwd_; }
    void set_cwd(const char* path);
    [[nodiscard]] TaskState state() const { return state_; }
    [[nodiscard]] uintptr_t page_table_root_pa() const;

    [[nodiscard]] MemoryDesc* memory() const { return owned_memory_ ? owned_memory_.get() : borrowed_memory_; }
    void use_kernel_memory() {
        assert(!owned_memory_ && state_ == TaskState::Uninit);
        borrowed_memory_ = &vmm::Manager::kernel_mm();
    }
    // Only an unpublished or non-running child may receive a prepared address space.
    void adopt_memory(sys::unique_ptr<MemoryDesc> memory);
    [[nodiscard]] uintptr_t kernel_stack() const { return kernel_stack_.empty() ? boot_stack_ : kernel_stack_.base(); }
    [[nodiscard]] uintptr_t kernel_stack_top() const {
        return kernel_stack_.empty() ? boot_stack_top_ : kernel_stack_.base() + KSTACK_SIZE;
    }
    // Whether the address is in the unmapped page below this task's stack.
    [[nodiscard]] bool stack_guard_contains(uintptr_t address) const { return kernel_stack_.guards(address); }
    void copy_thread(uintptr_t stack_pointer, TrapFrame* src_tf);
    void setup_user_thread(uintptr_t entry, uintptr_t stack);
    Error setup_kernel_stack();
    [[nodiscard]] fd::Table& files() { return files_; }
    [[nodiscard]] const fd::Table& files() const { return files_; }

    void set_links();
    void remove_links();
    void destroy();

    friend class TaskManager;
};

using TaskList = sys::intrusive_list<Task, &Task::list_node>;
using TaskHashList = sys::intrusive_list<Task, &Task::hash_node>;

class SchedulerPolicy {
public:
    [[nodiscard]] const char* name() const;
    [[nodiscard]] int calc_time_slice(int priority) const;
    void tick(Task* current, Task* idle) const;
    // Whether `woken`, just made runnable, should take the CPU from `current`
    // now rather than when its quantum ends.
    [[nodiscard]] bool preempts(const Task& woken, const Task& current, const Task* idle) const;
    [[nodiscard]] Task* pick_next(TaskList& proc_list, Task* idle, Task* current = nullptr);
    void task_removed(const Task& task);

private:
    Task* cursor_{};
};

// CPU scheduling owns the active task, policy cursor and telemetry.
class Scheduler {
public:
    static Task* current() { return current_; }
    static void set_current(Task* task) { current_ = task; }
    static int time_slice(int priority) { return policy_.calc_time_slice(priority); }
    static const char* policy_name() { return policy_.name(); }
    static void task_removed(const Task& task) { policy_.task_removed(task); }
    static void schedule();
    static void tick();
    // A task became runnable; asks the running one to give way if it should.
    static void task_woken(Task& task);
    static void print_stats();

private:
    inline static Task* current_{};
    inline static SchedulerPolicy policy_{};
    // Scheduler telemetry counters (monotonic since boot)
    inline static uint64_t tick_count_{};
    inline static uint64_t schedule_calls_{};
    inline static uint64_t need_resched_events_{};
    inline static uint64_t context_switches_{};
    inline static uint64_t same_task_runs_{};
    inline static uint64_t pick_idle_{};
    inline static uint64_t pick_non_idle_{};
};

class TaskManager {
public:
    static constexpr int HASH_SHIFT = 10;
    static constexpr size_t HASH_LIST_SIZE = 1 << HASH_SHIFT;

    static int init();

    static TaskList& task_list() { return proc_list_; }
    static Task* idle_task() { return idle_proc_; }
    static Task* init_task() { return init_proc_; }
    static int process_count() { return process_count_; }

    static inline TaskHashList& hash_list(int pid) { return hash_list_[pid_hash(pid)]; }

    static void add_process(Task* proc);
    static void remove_process(Task* proc);

    static Task* find_process(int pid);

    static void print();
    static Result<bool> next_process(int after_pid, ProcessInfo& info);

    // Publication, parent/child relationships and process resource lifetime.
    static Result<int> fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* trap_frame);
    static Result<int> kernel_thread(KernelThreadEntry fn, void* arg);
    static Result<int> spawn_user(sys::unique_ptr<MemoryDesc> memory, uintptr_t entry, uintptr_t stack,
                                  const char* name, bool foreground = false);
    static Error terminate(int pid);
    static Error interrupt_console(int pid);
    static void interrupt_job(int owner);
    static void interrupt_pty(const pty::Session* session, int except_pid = 0);
    static bool has_live_job(int owner);
    static Result<int> wait_ready(int pid, int* code_store);
    static int exit(int error_code);
    static Result<int> wait(int pid, int* code_store);

private:
    friend struct Task;
    inline static TaskList proc_list_{};
    inline static sys::array<TaskHashList, HASH_LIST_SIZE> hash_list_{};
    inline static Task* idle_proc_{};  // Idle process (PID 0)
    inline static Task* init_proc_{};  // Init process (PID 1)
    inline static int process_count_{};
    inline static int next_pid_ = 1;

    static uint32_t pid_hash(int x);
    static Result<int> publish(sys::unique_ptr<Task> task, bool foreground = false);
    static Error request_termination(Task* target);

    // Initialization helpers
    static int init_idle();
    static int init_init_proc();
};

inline void Task::adopt_memory(sys::unique_ptr<MemoryDesc> memory) {
    assert(memory && !owned_memory_ && this != Scheduler::current());
    assert(state_ == TaskState::Uninit || state_ == TaskState::Runnable);
    owned_memory_ = sys::move(memory);
    borrowed_memory_ = nullptr;
}

inline Task::~Task() {
    // Published tasks must be off CPU and detached before resource release.
    assert(this != Scheduler::current());
    assert(!list_node.is_linked() && !hash_node.is_linked() && !child_node.is_linked() && child_list.empty());
    files_.close_all();
    kernel_stack_ = KernelStack{};
    // owned_memory_ releases the address space after files and stack; borrows are untouched.
}

namespace sched {

int init();

void schedule();
void tick();  // Called from timer ISR each tick
Result<int> fork(uint32_t clone_flags, uintptr_t stack, TrapFrame* tf);
Result<int> kernel_thread(KernelThreadEntry fn, void* arg);
int exit(int error_code);
Result<int> wait(int pid, int* code_store);

Task* current();
Task* find_process(int pid);
void print();
void print_stats();

}  // namespace sched
