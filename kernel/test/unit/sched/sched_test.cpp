#include "sched/sched.h"
#include "mm/vmm.h"
#include "drivers/intr.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "test/task_access.h"

// External symbols
extern long user_stack[];

// Test statistics
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name)            \
    cprintf("\n[TEST] %s\n", name); \
    int zonix_test_result = 1;

#define TEST_ASSERT(cond, msg)         \
    if (!(cond)) {                     \
        cprintf("  [FAIL] %s\n", msg); \
        zonix_test_result = 0;         \
    } else {                           \
        cprintf("  [OK] %s\n", msg);   \
    }

#define TEST_END()               \
    if (zonix_test_result) {     \
        cprintf("  [PASSED]\n"); \
        tests_passed++;          \
    } else {                     \
        cprintf("  [FAILED]\n"); \
        tests_failed++;          \
    }

// Helper: set up a test Task so it won't crash the scheduler.
// The real scheduler calls page_table_root_pa() which requires a valid memory()->pgdir.
// Without this, a timer interrupt during the test would panic the kernel.
static void init_test_proc(Task* proc, int pid) {
    proc->pid = pid;
    proc->use_kernel_memory();
    proc->priority = sched_prio::IDLE_PRIO;
    proc->time_slice = 0;
}

// ============================================================================
// Unit Tests - Process Creation
// ============================================================================

static void test_process_creation() {
    TEST_START("Process Creation");
    const size_t free_before = pmm::free_page_count();

    // Create a new process
    Task* proc = new Task();
    TEST_ASSERT(proc != nullptr, "Task allocation succeeds");

    // Check default state
    TEST_ASSERT(proc->state() == TaskState::Uninit, "Initial state is Uninit");
    TEST_ASSERT(proc->pid == 0, "Initial PID is 0");
    TEST_ASSERT(proc->parent == nullptr, "Initial parent is nullptr");
    TEST_ASSERT(TaskAccess::kernel_stack(proc) == 0, "Initial kernel stack is 0");
    TEST_ASSERT(proc->memory() == nullptr, "Initial memory is nullptr");

    // Setup kernel stack
    Error ret = proc->setup_kernel_stack();
    TEST_ASSERT(ret == Error::None, "Kernel stack setup succeeds");
    TEST_ASSERT(TaskAccess::kernel_stack(proc) != 0, "Kernel stack is allocated");
    const uintptr_t stack = proc->kernel_stack();
    TEST_ASSERT(proc->setup_kernel_stack() == Error::Busy && proc->kernel_stack() == stack,
                "Repeated setup preserves the existing stack");
    TEST_ASSERT(proc->child_list.empty(), "Child list is empty after init");

    // Task owns its dynamic stack.
    delete proc;
    TEST_ASSERT(pmm::free_page_count() == free_before, "Task destruction returns object and stack pages");

    TEST_END();
}

// ============================================================================
// Unit Tests - Process State Transitions
// ============================================================================

static void test_process_state_transitions() {
    TEST_START("Process State Transitions");

    Task* proc = new Task();

    // Test initial state
    TEST_ASSERT(proc->state() == TaskState::Uninit, "Initial state is Uninit");

    proc->sleep();
    TEST_ASSERT(proc->state() == TaskState::Sleeping, "sleep() sets state to Sleeping");
    proc->wakeup();
    TEST_ASSERT(proc->state() == TaskState::Runnable, "wakeup() sets state to Runnable");

    // Multiple wakeup calls should keep it Runnable
    proc->wakeup();
    TEST_ASSERT(proc->state() == TaskState::Runnable, "Multiple wakeup() keeps state Runnable");

    delete proc;

    TEST_END();
}

// ============================================================================
// Unit Tests - Process List Management
// ============================================================================

static void test_process_list_management() {
    TEST_START("Process List Management");

    int initial_count = TaskManager::process_count();

    // Create test processes
    Task* proc1 = new Task();
    Task* proc2 = new Task();
    Task* proc3 = new Task();

    init_test_proc(proc1, 100);
    init_test_proc(proc2, 101);
    init_test_proc(proc3, 102);

    // Add processes
    TaskManager::add_process(proc1);
    TEST_ASSERT(TaskManager::process_count() == initial_count + 1, "Process count increases after add");

    TaskManager::add_process(proc2);
    TaskManager::add_process(proc3);
    TEST_ASSERT(TaskManager::process_count() == initial_count + 3, "Process count correct after adding 3");

    // Find processes
    Task* found = TaskManager::find_process(100);
    TEST_ASSERT(found == proc1, "find_process returns correct process (PID 100)");

    found = TaskManager::find_process(101);
    TEST_ASSERT(found == proc2, "find_process returns correct process (PID 101)");

    found = TaskManager::find_process(102);
    TEST_ASSERT(found == proc3, "find_process returns correct process (PID 102)");

    found = TaskManager::find_process(999);
    TEST_ASSERT(found == nullptr, "find_process returns nullptr for non-existent PID");

    found = TaskManager::find_process(0);
    TEST_ASSERT(found == nullptr, "find_process returns nullptr for PID 0");

    found = TaskManager::find_process(-1);
    TEST_ASSERT(found == nullptr, "find_process returns nullptr for negative PID");

    // Remove processes
    TaskManager::remove_process(proc2);
    TEST_ASSERT(TaskManager::process_count() == initial_count + 2, "Process count decreases after remove");

    found = TaskManager::find_process(101);
    TEST_ASSERT(found == nullptr, "Removed process not found");

    // Clean up remaining processes
    TaskManager::remove_process(proc1);
    TaskManager::remove_process(proc3);
    TEST_ASSERT(TaskManager::process_count() == initial_count, "Process count restored after cleanup");

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

    int initial_count = TaskManager::process_count();

    // Create processes with different PIDs that may hash to same/different buckets
    Task* procs[10];
    for (int i = 0; i < 10; i++) {
        procs[i] = new Task();
        init_test_proc(procs[i], 1000 + i * 17);
        TaskManager::add_process(procs[i]);
    }

    TEST_ASSERT(TaskManager::process_count() == initial_count + 10, "10 processes added");

    // Verify all can be found
    bool all_found = true;
    for (int i = 0; i < 10; i++) {
        Task* found = TaskManager::find_process(1000 + i * 17);
        if (found != procs[i]) {
            all_found = false;
            break;
        }
    }
    TEST_ASSERT(all_found, "All 10 processes found in hash table");

    // Remove and verify
    TaskManager::remove_process(procs[5]);
    Task* found = TaskManager::find_process(1000 + 5 * 17);
    TEST_ASSERT(found == nullptr, "Removed process not found in hash table");

    // Other processes still findable
    found = TaskManager::find_process(1000 + 4 * 17);
    TEST_ASSERT(found == procs[4], "Adjacent process still findable after removal");

    // Clean up
    for (int i = 0; i < 10; i++) {
        if (i != 5) {
            TaskManager::remove_process(procs[i]);
        }
        delete procs[i];
    }

    TEST_ASSERT(TaskManager::process_count() == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Parent-Child Relationships
// ============================================================================

static void test_parent_child_relationships() {
    TEST_START("Parent-Child Relationships");

    int initial_count = TaskManager::process_count();

    // Create parent process
    Task* parent = new Task();
    init_test_proc(parent, 200);
    TaskManager::add_process(parent);

    // Create child processes
    Task* child1 = new Task();
    Task* child2 = new Task();

    init_test_proc(child1, 201);
    init_test_proc(child2, 202);

    child1->parent = parent;
    child2->parent = parent;

    // Add children using set_links (simulating fork behavior)
    TaskManager::add_process(child1);
    parent->child_list.push_front(*child1);

    TaskManager::add_process(child2);
    parent->child_list.push_front(*child2);

    TEST_ASSERT(!parent->child_list.empty(), "Parent has children");

    // Verify children through list traversal
    int child_count = 0;
    for ([[maybe_unused]] const Task& child : parent->child_list) {
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

    TEST_ASSERT(TaskManager::process_count() == initial_count, "Process count restored");

    TEST_END();
}

// ============================================================================
// Unit Tests - Idle and Init Processes
// ============================================================================

static void test_idle_init_processes() {
    TEST_START("Idle and Init Processes");

    // Verify idle process
    TEST_ASSERT(TaskManager::idle_task() != nullptr, "Idle process exists");
    TEST_ASSERT(TaskManager::idle_task()->pid == 0, "Idle process has PID 0");

    // Verify init process
    TEST_ASSERT(TaskManager::init_task() != nullptr, "Init process exists");
    TEST_ASSERT(TaskManager::init_task()->pid == 1, "Init process has PID 1");

    // Verify current process
    Task* current = Scheduler::current();
    TEST_ASSERT(current != nullptr, "Current process is set");

    // Verify init is child of idle (or has idle as parent based on fork)
    TEST_ASSERT(TaskManager::init_task()->parent == TaskManager::idle_task(), "Init process parent is idle");

    TEST_END();
}

// ============================================================================
// Unit Tests - Process Destruction
// ============================================================================

static void test_process_destruction() {
    TEST_START("Process Destruction");

    int initial_count = TaskManager::process_count();

    // Create a process with kernel stack
    Task* proc = new Task();
    init_test_proc(proc, 300);

    Error ret = proc->setup_kernel_stack();
    TEST_ASSERT(ret == Error::None, "Kernel stack allocated for destruction test");

    uintptr_t kstack_addr = TaskAccess::kernel_stack(proc);
    TEST_ASSERT(kstack_addr != 0, "Kernel stack address is non-zero");
    TEST_ASSERT(kstack_addr != reinterpret_cast<uintptr_t>(user_stack), "Kernel stack is not boot stack");

    TaskManager::add_process(proc);
    TEST_ASSERT(TaskManager::process_count() == initial_count + 1, "Process added");

    // Detach from the scheduler before reaping the zombie.
    TaskManager::remove_process(proc);
    TEST_ASSERT(TaskManager::process_count() == initial_count, "Process removed from list");

    // Reaping releases the detached zombie's stack.
    proc->mark_zombie(0);
    proc->destroy();

    TEST_END();
}

// ============================================================================
// Real policy regression (isolated list, no fake task enters the live scheduler).
static void test_policy_removal_and_current() {
    TEST_START("Policy cursor removal, running priority and round robin");
    SchedulerPolicy policy;
    TaskList head;
    Task* first = new Task();
    Task* victim = new Task();
    Task* last = new Task();
    if (!first || !victim || !last) {
        TEST_ASSERT(false, "Allocated policy fixtures");
        delete first;
        delete victim;
        delete last;
        TEST_END();
        return;
    }
    first->wakeup();
    victim->wakeup();
    last->wakeup();
    head.push_back(*first);
    head.push_back(*victim);
    head.push_back(*last);
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task()) == first, "First task selected");
    policy.task_removed(*victim);
    victim->list_node.unlink();
    delete victim;
    last->priority = 0;
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task()) == last,
                "Freed cursor target cannot poison traversal");
    first->mark_running();
    first->priority = 0;
    last->priority = 10;
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task(), first) == first,
                "Running highest-priority task stays on CPU");
    last->sleep();
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task(), first) == first,
                "Only running task does not bounce through idle");
    last->wakeup();
    last->priority = 0;
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task(), first) == last,
                "Equal-priority peer receives next quantum");
    TEST_ASSERT(policy.pick_next(head, TaskManager::idle_task(), first) == first,
                "Round robin returns to running peer");
    first->list_node.unlink();
    last->list_node.unlink();
    delete first;
    delete last;
    TEST_END();
}

// Main Test Runner
// ============================================================================

namespace sched {

void test() {
    // Disable interrupts for the entire test suite.
    // Tests add fake Task objects (no valid context/stack) to the process
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
    test_process_destruction();

    test_policy_removal_and_current();

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
