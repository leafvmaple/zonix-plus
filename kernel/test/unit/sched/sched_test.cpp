#include "sched/sched.h"
#include "mm/vmm.h"
#include "drivers/intr.h"
#include "lib/stdio.h"
#include "lib/memory.h"

// External symbols
extern long user_stack[];
extern MemoryDesc init_mm;

struct TaskStructAccess {
    static uintptr_t kernel_stack(const TaskStruct* proc) { return proc->kernel_stack_; }
};

// Test statistics
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name)            \
    cprintf("\n[TEST] %s\n", name); \
    int __test_result = 1;

#define TEST_ASSERT(cond, msg)         \
    if (!(cond)) {                     \
        cprintf("  [FAIL] %s\n", msg); \
        __test_result = 0;             \
    } else {                           \
        cprintf("  [OK] %s\n", msg);   \
    }

#define TEST_END()               \
    if (__test_result) {         \
        cprintf("  [PASSED]\n"); \
        tests_passed++;          \
    } else {                     \
        cprintf("  [FAILED]\n"); \
        tests_failed++;          \
    }

// Helper: set up a test TaskStruct so it won't crash the scheduler.
// The real scheduler calls get_cr3() which asserts memory->pgdir != nullptr.
// Without this, a timer interrupt during the test would panic the kernel.
static void init_test_proc(TaskStruct* proc, int pid) {
    proc->pid = pid;
    proc->memory = &init_mm;
    proc->priority = sched_prio::IDLE_PRIO;
    proc->time_slice = 0;
}

// ============================================================================
// Unit Tests - Process Creation
// ============================================================================

static void test_process_creation() {
    TEST_START("Process Creation");

    // Create a new process
    TaskStruct* proc = new TaskStruct();
    TEST_ASSERT(proc != nullptr, "TaskStruct allocation succeeds");

    // Check default state
    TEST_ASSERT(proc->get_state() == ProcessState::Uninit, "Initial state is Uninit");
    TEST_ASSERT(proc->pid == 0, "Initial PID is 0");
    TEST_ASSERT(proc->parent == nullptr, "Initial parent is nullptr");
    TEST_ASSERT(TaskStructAccess::kernel_stack(proc) == 0, "Initial kernel stack is 0");
    TEST_ASSERT(proc->memory == nullptr, "Initial memory is nullptr");

    // Setup kernel stack
    int ret = proc->setup_kernel_stack();
    TEST_ASSERT(ret == 0, "Kernel stack setup succeeds");
    TEST_ASSERT(TaskStructAccess::kernel_stack(proc) != 0, "Kernel stack is allocated");
    TEST_ASSERT(proc->child_list.empty(), "Child list is empty after init");

    // Clean up - free the kernel stack
    kfree(reinterpret_cast<void*>(TaskStructAccess::kernel_stack(proc)));
    delete proc;

    TEST_END();
}

// ============================================================================
// Unit Tests - Process State Transitions
// ============================================================================

static void test_process_state_transitions() {
    TEST_START("Process State Transitions");

    TaskStruct* proc = new TaskStruct();

    // Test initial state
    TEST_ASSERT(proc->get_state() == ProcessState::Uninit, "Initial state is Uninit");

    // Transition: Uninit -> Runnable (via wakeup)
    proc->sleep();  // Set to sleeping first
    proc->wakeup();
    TEST_ASSERT(proc->get_state() == ProcessState::Runnable, "wakeup() sets state to Runnable");

    // Multiple wakeup calls should keep it Runnable
    proc->wakeup();
    TEST_ASSERT(proc->get_state() == ProcessState::Runnable, "Multiple wakeup() keeps state Runnable");

    delete proc;

    TEST_END();
}

// ============================================================================
// Unit Tests - Process List Management
// ============================================================================

static void test_process_list_management() {
    TEST_START("Process List Management");

    int initial_count = TaskManager::s_process_count;

    // Create test processes
    TaskStruct* proc1 = new TaskStruct();
    TaskStruct* proc2 = new TaskStruct();
    TaskStruct* proc3 = new TaskStruct();

    init_test_proc(proc1, 100);
    init_test_proc(proc2, 101);
    init_test_proc(proc3, 102);

    // Add processes
    TaskManager::add_process(proc1);
    TEST_ASSERT(TaskManager::s_process_count == initial_count + 1, "Process count increases after add");

    TaskManager::add_process(proc2);
    TaskManager::add_process(proc3);
    TEST_ASSERT(TaskManager::s_process_count == initial_count + 3, "Process count correct after adding 3");

    // Find processes
    TaskStruct* found = TaskManager::find_proc(100);
    TEST_ASSERT(found == proc1, "find_proc returns correct process (PID 100)");

    found = TaskManager::find_proc(101);
    TEST_ASSERT(found == proc2, "find_proc returns correct process (PID 101)");

    found = TaskManager::find_proc(102);
    TEST_ASSERT(found == proc3, "find_proc returns correct process (PID 102)");

    found = TaskManager::find_proc(999);
    TEST_ASSERT(found == nullptr, "find_proc returns nullptr for non-existent PID");

    found = TaskManager::find_proc(0);
    TEST_ASSERT(found == nullptr, "find_proc returns nullptr for PID 0");

    found = TaskManager::find_proc(-1);
    TEST_ASSERT(found == nullptr, "find_proc returns nullptr for negative PID");

    // Remove processes
    TaskManager::remove_process(proc2);
    TEST_ASSERT(TaskManager::s_process_count == initial_count + 2, "Process count decreases after remove");

    found = TaskManager::find_proc(101);
    TEST_ASSERT(found == nullptr, "Removed process not found");

    // Clean up remaining processes
    TaskManager::remove_process(proc1);
    TaskManager::remove_process(proc3);
    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored after cleanup");

    delete proc1;
    delete proc2;
    delete proc3;

    TEST_END();
}

// ============================================================================
// Unit Tests - Hash Table
// ============================================================================

static void test_hash_table() {
    TEST_START("Hash Table Functionality");

    int initial_count = TaskManager::s_process_count;

    // Create processes with different PIDs that may hash to same/different buckets
    TaskStruct* procs[10];
    for (int i = 0; i < 10; i++) {
        procs[i] = new TaskStruct();
        init_test_proc(procs[i], 1000 + i * 17);
        TaskManager::add_process(procs[i]);
    }

    TEST_ASSERT(TaskManager::s_process_count == initial_count + 10, "10 processes added");

    // Verify all can be found
    bool all_found = true;
    for (int i = 0; i < 10; i++) {
        TaskStruct* found = TaskManager::find_proc(1000 + i * 17);
        if (found != procs[i]) {
            all_found = false;
            break;
        }
    }
    TEST_ASSERT(all_found, "All 10 processes found in hash table");

    // Remove and verify
    TaskManager::remove_process(procs[5]);
    TaskStruct* found = TaskManager::find_proc(1000 + 5 * 17);
    TEST_ASSERT(found == nullptr, "Removed process not found in hash table");

    // Other processes still findable
    found = TaskManager::find_proc(1000 + 4 * 17);
    TEST_ASSERT(found == procs[4], "Adjacent process still findable after removal");

    // Clean up
    for (int i = 0; i < 10; i++) {
        if (i != 5) {
            TaskManager::remove_process(procs[i]);
        }
        delete procs[i];
    }

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Parent-Child Relationships
// ============================================================================

static void test_parent_child_relationships() {
    TEST_START("Parent-Child Relationships");

    int initial_count = TaskManager::s_process_count;

    // Create parent process
    TaskStruct* parent = new TaskStruct();
    init_test_proc(parent, 200);
    TaskManager::add_process(parent);

    // Create child processes
    TaskStruct* child1 = new TaskStruct();
    TaskStruct* child2 = new TaskStruct();

    init_test_proc(child1, 201);
    init_test_proc(child2, 202);

    child1->parent = parent;
    child2->parent = parent;

    // Add children using set_links (simulating fork behavior)
    TaskManager::add_process(child1);
    parent->child_list.add(child1->child_node);

    TaskManager::add_process(child2);
    parent->child_list.add(child2->child_node);

    TEST_ASSERT(!parent->child_list.empty(), "Parent has children");

    // Verify children through list traversal
    int child_count = 0;
    ListNode* head = &parent->child_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        child_count++;
    }
    TEST_ASSERT(child_count == 2, "Parent has 2 children in list");

    // Verify child->parent relationship
    TEST_ASSERT(child1->parent == parent, "Child1 parent pointer correct");
    TEST_ASSERT(child2->parent == parent, "Child2 parent pointer correct");

    // Clean up - remove child links first
    child1->child_node.unlink();
    child2->child_node.unlink();

    TEST_ASSERT(parent->child_list.empty(), "Parent child list empty after unlinking");

    TaskManager::remove_process(child1);
    TaskManager::remove_process(child2);
    TaskManager::remove_process(parent);

    delete child1;
    delete child2;
    delete parent;

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Idle and Init Processes
// ============================================================================

static void test_idle_init_processes() {
    TEST_START("Idle and Init Processes");

    // Verify idle process
    TEST_ASSERT(TaskManager::s_idle_proc != nullptr, "Idle process exists");
    TEST_ASSERT(TaskManager::s_idle_proc->pid == 0, "Idle process has PID 0");

    // Verify init process
    TEST_ASSERT(TaskManager::s_init_proc != nullptr, "Init process exists");
    TEST_ASSERT(TaskManager::s_init_proc->pid == 1, "Init process has PID 1");

    // Verify current process
    TaskStruct* current = TaskManager::get_current();
    TEST_ASSERT(current != nullptr, "Current process is set");

    // Verify init is child of idle (or has idle as parent based on fork)
    TEST_ASSERT(TaskManager::s_init_proc->parent == TaskManager::s_idle_proc, "Init process parent is idle");

    TEST_END();
}

// ============================================================================
// Unit Tests - Context Structure
// ============================================================================

static void test_context_structure() {
    TEST_START("Context Structure");

    Context ctx{};

#if defined(__x86_64__)
    // Verify default initialization
    TEST_ASSERT(ctx.rip == 0, "Context rip initialized to 0");
    TEST_ASSERT(ctx.rsp == 0, "Context rsp initialized to 0");
    TEST_ASSERT(ctx.rbx == 0, "Context rbx initialized to 0");
    TEST_ASSERT(ctx.rbp == 0, "Context rbp initialized to 0");

    // Set and verify values
    ctx.rip = 0x12345678;
    ctx.rsp = 0xDEADBEEF;
    TEST_ASSERT(ctx.rip == 0x12345678, "Context rip set correctly");
    TEST_ASSERT(ctx.rsp == 0xDEADBEEF, "Context rsp set correctly");
#elif defined(__aarch64__)
    // Verify default initialization
    TEST_ASSERT(ctx.x30 == 0, "Context x30/LR initialized to 0");
    TEST_ASSERT(ctx.sp == 0, "Context sp initialized to 0");

    // Set and verify values
    ctx.x30 = 0x12345678;
    ctx.sp = 0xDEADBEEF;
    TEST_ASSERT(ctx.x30 == 0x12345678, "Context x30/LR set correctly");
    TEST_ASSERT(ctx.sp == 0xDEADBEEF, "Context sp set correctly");
#endif

    TEST_END();
}

// ============================================================================
// Unit Tests - Process Destruction
// ============================================================================

static void test_process_destruction() {
    TEST_START("Process Destruction");

    int initial_count = TaskManager::s_process_count;

    // Create a process with kernel stack
    TaskStruct* proc = new TaskStruct();
    init_test_proc(proc, 300);

    int ret = proc->setup_kernel_stack();
    TEST_ASSERT(ret == 0, "Kernel stack allocated for destruction test");

    uintptr_t kstack_addr = TaskStructAccess::kernel_stack(proc);
    TEST_ASSERT(kstack_addr != 0, "Kernel stack address is non-zero");
    TEST_ASSERT(kstack_addr != reinterpret_cast<uintptr_t>(user_stack), "Kernel stack is not boot stack");

    TaskManager::add_process(proc);
    TEST_ASSERT(TaskManager::s_process_count == initial_count + 1, "Process added");

    // Simulate destruction (without calling destroy() which would delete)
    TaskManager::remove_process(proc);
    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process removed from list");

    // Free kernel stack manually
    kfree(reinterpret_cast<void*>(TaskStructAccess::kernel_stack(proc)));
    delete proc;

    TEST_END();
}

// ============================================================================
// Unit Tests - Scheduler Selection
// ============================================================================

static void test_scheduler_selection() {
    TEST_START("Scheduler Process Selection");

    int initial_count = TaskManager::s_process_count;

    // Create multiple processes with different states
    TaskStruct* proc1 = new TaskStruct();
    TaskStruct* proc2 = new TaskStruct();
    TaskStruct* proc3 = new TaskStruct();

    init_test_proc(proc1, 500);
    init_test_proc(proc2, 501);
    init_test_proc(proc3, 502);

    // Set different states
    proc1->sleep();   // Should NOT be selected
    proc2->wakeup();  // Should be selected (first runnable)
    proc3->wakeup();  // Should NOT be selected (second runnable)

    TaskManager::add_process(proc1);
    TaskManager::add_process(proc2);
    TaskManager::add_process(proc3);

    // Verify process states
    TEST_ASSERT(proc1->get_state() == ProcessState::Sleeping, "proc1 is Sleeping");
    TEST_ASSERT(proc2->get_state() == ProcessState::Runnable, "proc2 is Runnable");
    TEST_ASSERT(proc3->get_state() == ProcessState::Runnable, "proc3 is Runnable");

    // Test that schedule would select first runnable process
    // We can't call schedule() directly as it would switch context,
    // so we simulate the selection logic
    TaskStruct* next = TaskManager::s_idle_proc;
    ListNode* head = &TaskManager::s_proc_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* proc = TaskStruct::from_list_link(le);
        if (proc->get_state() == ProcessState::Runnable) {
            next = proc;
            break;
        }
    }

    // The first runnable in our test processes should be found
    // (Note: idle/init are also in the list, so we just verify a runnable is selected)
    TEST_ASSERT(next->get_state() == ProcessState::Runnable, "Scheduler selects a runnable process");

    // Clean up
    TaskManager::remove_process(proc1);
    TaskManager::remove_process(proc2);
    TaskManager::remove_process(proc3);

    delete proc1;
    delete proc2;
    delete proc3;

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Process State During Scheduling
// ============================================================================

static void test_process_state_scheduling() {
    TEST_START("Process State During Scheduling");

    TaskStruct* current = TaskManager::get_current();
    TEST_ASSERT(current != nullptr, "Current process exists");

    // Current process should be Running
    TEST_ASSERT(current->get_state() == ProcessState::Running || current->get_state() == ProcessState::Runnable,
                "Current process is Running or Runnable");

    // Verify idle process state
    TEST_ASSERT(TaskManager::s_idle_proc != nullptr, "Idle process exists");

    // Verify init process state
    TEST_ASSERT(TaskManager::s_init_proc != nullptr, "Init process exists");
    TEST_ASSERT(TaskManager::s_init_proc->get_state() != ProcessState::Zombie, "Init process is not Zombie");

    TEST_END();
}

// ============================================================================
// Unit Tests - Runnable Queue Behavior
// ============================================================================

static void test_runnable_queue() {
    TEST_START("Runnable Queue Behavior");

    int initial_count = TaskManager::s_process_count;

    // Create processes and add to runnable queue
    constexpr int NUM_PROCS = 5;
    TaskStruct* procs[NUM_PROCS];

    for (int i = 0; i < NUM_PROCS; i++) {
        procs[i] = new TaskStruct();
        init_test_proc(procs[i], 600 + i);
        procs[i]->wakeup();
        TaskManager::add_process(procs[i]);
    }

    TEST_ASSERT(TaskManager::s_process_count == initial_count + NUM_PROCS, "All processes added to queue");

    // Count runnable processes (excluding our test procs, count only new ones)
    int runnable_count = 0;
    ListNode* head = &TaskManager::s_proc_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* proc = TaskStruct::from_list_link(le);
        if (proc->get_state() == ProcessState::Runnable) {
            runnable_count++;
        }
    }
    TEST_ASSERT(runnable_count >= NUM_PROCS, "At least NUM_PROCS runnable processes");

    // Set some to sleeping, verify they won't be scheduled
    procs[0]->sleep();
    procs[2]->sleep();

    runnable_count = 0;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* proc = TaskStruct::from_list_link(le);
        // Only count our test procs
        if (proc->pid >= 600 && proc->pid < 600 + NUM_PROCS) {
            if (proc->get_state() == ProcessState::Runnable) {
                runnable_count++;
            }
        }
    }
    TEST_ASSERT(runnable_count == 3, "Only 3 of our test procs are runnable after sleeping 2");

    // Clean up
    for (int i = 0; i < NUM_PROCS; i++) {
        TaskManager::remove_process(procs[i]);
        delete procs[i];
    }

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Sleep and Wakeup
// ============================================================================

static void test_sleep_wakeup() {
    TEST_START("Sleep and Wakeup");

    int initial_count = TaskManager::s_process_count;

    TaskStruct* proc = new TaskStruct();
    init_test_proc(proc, 700);
    proc->wakeup();
    TaskManager::add_process(proc);

    // Test sleep transition
    proc->sleep();
    TEST_ASSERT(proc->get_state() == ProcessState::Sleeping, "Process is sleeping");

    // Verify sleeping process won't be selected by scheduler
    bool would_be_selected = false;
    ListNode* head = &TaskManager::s_proc_list;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* p = TaskStruct::from_list_link(le);
        if (p == proc && p->get_state() == ProcessState::Runnable) {
            would_be_selected = true;
            break;
        }
    }
    TEST_ASSERT(!would_be_selected, "Sleeping process not selected by scheduler");

    // Test wakeup
    proc->wakeup();
    TEST_ASSERT(proc->get_state() == ProcessState::Runnable, "Process woken up to Runnable");

    // Now it should be selectable
    would_be_selected = false;
    for (auto* le = head->get_next(); le != head; le = le->get_next()) {
        TaskStruct* p = TaskStruct::from_list_link(le);
        if (p == proc && p->get_state() == ProcessState::Runnable) {
            would_be_selected = true;
            break;
        }
    }
    TEST_ASSERT(would_be_selected, "Woken process can be selected by scheduler");

    // Clean up
    TaskManager::remove_process(proc);
    delete proc;

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Wait State
// ============================================================================

static void test_wait_state() {
    TEST_START("Wait State Management");

    int initial_count = TaskManager::s_process_count;

    // Create parent
    TaskStruct* parent = new TaskStruct();
    init_test_proc(parent, 800);
    parent->wakeup();
    parent->wait_state = 0;
    TaskManager::add_process(parent);

    // Create child
    TaskStruct* child = new TaskStruct();
    init_test_proc(child, 801);
    child->parent = parent;
    child->wakeup();
    TaskManager::add_process(child);
    parent->child_list.add(child->child_node);

    // Simulate parent waiting for child
    parent->wait_state = 1;
    parent->sleep();

    TEST_ASSERT(parent->wait_state == 1, "Parent wait_state is set");
    TEST_ASSERT(parent->get_state() == ProcessState::Sleeping, "Parent is sleeping while waiting");

    // Simulate child exit (becomes zombie)
    child->mark_zombie(42);

    TEST_ASSERT(child->get_state() == ProcessState::Zombie, "Child is zombie");
    TEST_ASSERT(child->exit_code == 42, "Child exit code preserved");

    // Parent wakes up when child exits (simulated)
    if (parent->wait_state) {
        parent->wakeup();
    }
    TEST_ASSERT(parent->get_state() == ProcessState::Runnable, "Parent woken up after child exit");

    // Reset wait state
    parent->wait_state = 0;
    TEST_ASSERT(parent->wait_state == 0, "Parent wait_state cleared");

    // Verify parent can retrieve exit code
    TEST_ASSERT(child->exit_code == 42, "Can retrieve child exit code");

    // Clean up
    child->child_node.unlink();
    TaskManager::remove_process(child);
    TaskManager::remove_process(parent);
    delete child;
    delete parent;

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Round Robin Fairness (Simulated)
// ============================================================================

static void test_round_robin_simulation() {
    TEST_START("Round Robin Fairness (Simulated)");

    int initial_count = TaskManager::s_process_count;

    // Create multiple runnable processes
    constexpr int NUM_PROCS = 4;
    TaskStruct* procs[NUM_PROCS];

    for (int i = 0; i < NUM_PROCS; i++) {
        procs[i] = new TaskStruct();
        init_test_proc(procs[i], 900 + i);
        procs[i]->wakeup();
        TaskManager::add_process(procs[i]);
    }

    // Simulate multiple schedule cycles
    // In round-robin, each process should get a chance
    int selection_count[NUM_PROCS] = {0};
    int total_selections = 0;

    for (int cycle = 0; cycle < NUM_PROCS * 2; cycle++) {
        // Find first runnable among OUR test procs only
        // (This simulates the scheduler's behavior for our processes)
        ListNode* head = &TaskManager::s_proc_list;
        for (auto* le = head->get_next(); le != head; le = le->get_next()) {
            TaskStruct* proc = TaskStruct::from_list_link(le);

            // Only consider our test processes (PID 900-903)
            if (proc->pid >= 900 && proc->pid < 900 + NUM_PROCS) {
                if (proc->get_state() == ProcessState::Runnable) {
                    int idx = proc->pid - 900;
                    selection_count[idx]++;
                    total_selections++;

                    // Simulate running: move this process to end of list
                    // by temporarily setting it to Running, then back
                    proc->mark_running();

                    // Unlink and re-add to end to simulate round-robin rotation
                    // Note: add_before(head) puts it at the END of the list
                    proc->list_node.unlink();
                    TaskManager::s_proc_list.add_before(proc->list_node);

                    proc->wakeup();
                    break;
                }
            }
        }
    }

    // Verify that selections happened
    TEST_ASSERT(total_selections == NUM_PROCS * 2, "All schedule cycles made a selection");

    // Verify round-robin fairness: each process should be selected at least once
    bool all_selected = true;
    for (int i = 0; i < NUM_PROCS; i++) {
        if (selection_count[i] == 0) {
            all_selected = false;
            break;
        }
    }
    TEST_ASSERT(all_selected, "All processes got at least one turn (round-robin)");

    // Clean up
    for (int i = 0; i < NUM_PROCS; i++) {
        TaskManager::remove_process(procs[i]);
        delete procs[i];
    }

    TEST_ASSERT(TaskManager::s_process_count == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Main Test Runner
// ============================================================================

namespace sched {

void test() {
    // Disable interrupts for the entire test suite.
    // Tests add fake TaskStruct objects (no valid context/stack) to the process
    // list with state = Runnable.  If a timer interrupt fires, the real
    // scheduler could select one and crash on context-switch.
    intr::Guard guard;

    cprintf("\n========================================\n");
    cprintf("       TaskManager Unit Tests\n");
    cprintf("========================================\n");

    tests_passed = 0;
    tests_failed = 0;

    // Data structure tests
    test_process_creation();
    test_process_state_transitions();
    test_process_list_management();
    test_hash_table();
    test_parent_child_relationships();
    test_idle_init_processes();
    test_context_structure();
    test_process_destruction();

    // Scheduling behavior tests
    test_scheduler_selection();
    test_process_state_scheduling();
    test_runnable_queue();
    test_sleep_wakeup();
    test_wait_state();
    test_round_robin_simulation();

    // Print summary
    cprintf("\n========================================\n");
    cprintf("       Test Summary\n");
    cprintf("========================================\n");
    cprintf("Passed: %d\n", tests_passed);
    cprintf("Failed: %d\n", tests_failed);
    cprintf("Total:  %d\n", tests_passed + tests_failed);

    if (tests_failed == 0) {
        cprintf("\n[SUCCESS] All TaskManager tests passed!\n");
    } else {
        cprintf("\n[FAILURE] Some tests failed!\n");
    }
    cprintf("========================================\n\n");
}

}  // namespace sched