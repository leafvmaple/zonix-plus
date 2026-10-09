#include "test/test_defs.h"
#include "sched/sched.h"
#include "lib/semaphore.h"
#include "lib/mutex.h"
#include "drivers/intr.h"
#include "trap/trap.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

struct SyncState {
    Semaphore ready;
    Semaphore gate;
    Semaphore done;
    Mutex mutex;
    int visits{};
};

int worker(void* arg) {
    auto& state = *static_cast<SyncState*>(arg);
    state.ready.up();
    state.gate.down();
    for (int i = 0; i < 128; ++i) {
        state.mutex.lock();
        ++state.visits;
        state.mutex.unlock();
        state.done.up();
        state.gate.down();
    }
    return 42;
}

struct WakeState {
    Semaphore ready;
    Semaphore done;
    WaitQueue queue;
    int visits{};
};

int queued_worker(void* arg) {
    auto& state = *static_cast<WakeState*>(arg);
    // Tell the parent only once enrollment and sleep form one interrupt window.
    intr::Guard guard;
    state.ready.up();
    state.queue.sleep();
    ++state.visits;
    state.done.up();
    return 0;
}

void test_multiple_wakeups(bool all) {
    TEST_START("Two stack wait nodes removed before either sleeper resumes");
    WakeState state;
    auto first = sched::kernel_thread(queued_worker, &state);
    auto second = sched::kernel_thread(queued_worker, &state);
    TEST_ASSERT(first.ok() && second.ok(), "Created both queue workers");
    if (first.ok()) {
        state.ready.down();
    }
    if (second.ok()) {
        state.ready.down();
    }
    {
        intr::Guard guard;
        if (all) {
            state.queue.wakeup_all();
        } else {
            state.queue.wakeup_one();
            state.queue.wakeup_one();
        }
    }
    if (first.ok()) {
        state.done.down();
        sched::wait(first.value(), nullptr).value_or(-1);
    }
    if (second.ok()) {
        state.done.down();
        sched::wait(second.value(), nullptr).value_or(-1);
    }
    TEST_ASSERT(state.visits == 2 && state.queue.empty(), "Both workers resume without resurrecting stack nodes");
    auto third = sched::kernel_thread(queued_worker, &state);
    if (third.ok()) {
        state.ready.down();
        state.queue.wakeup_one();
        state.done.down();
        auto reaped = sched::wait(third.value(), nullptr);
        TEST_ASSERT(reaped.ok() && state.visits == 3 && state.queue.empty(),
                    "Fresh enrollment still works after double wakeup");
    }
    TEST_END();
}

int user_exception_worker(void*) {
    TrapFrame tf{};
    arch_setup_user_tf(&tf, 0, 0);
    trap_dispatch(&tf);  // The default synchronous exception is unsupported on every ISA.
    return 99;
}

void test_user_exception() {
    TEST_START("Unhandled user exception terminates only its task");
    auto child = sched::kernel_thread(user_exception_worker, nullptr);
    if (child.ok()) {
        int code{};
        auto result = sched::wait(child.value(), &code);
        TEST_ASSERT(result.ok() && code == -1, "Faulting child exits and parent keeps running");
    } else {
        TEST_ASSERT(false, "Created faulting child");
    }
    TEST_END();
}

}  // namespace

namespace sync_test {

void test() {
    tests_passed = tests_failed = 0;
    TEST_START("Blocking synchronization and wait for an absent child PID");
    SyncState state;
    bool irq_before = arch_irq_is_enabled();
    auto child = sched::kernel_thread(worker, &state);
    TEST_ASSERT(child.ok(), "Created synchronization worker");
    if (!child.ok()) {
        TEST_END();
        return;
    }
    state.ready.down();
    auto missing = sched::wait(child.value() + 1000000, nullptr);
    TEST_ASSERT(!missing.ok() && missing.error() == Error::NotFound,
                "Waiting for absent PID returns despite another live child");
    for (int i = 0; i < 128; ++i) {
        state.mutex.lock();
        state.gate.up();
        // Let the worker block on the mutex while this task owns it.
        sched::schedule();
        state.mutex.unlock();
        state.done.down();
    }
    state.gate.up();
    int code = 0;
    auto reaped = sched::wait(child.value(), &code);
    TEST_ASSERT(reaped.ok() && reaped.value() == child.value() && code == 42, "Worker exits and is reaped");
    TEST_ASSERT(state.visits == 128, "All semaphore/mutex round trips completed");
    TEST_ASSERT(arch_irq_is_enabled() == irq_before, "Blocking operations restore interrupt state");
    TEST_END();
    test_multiple_wakeups(false);
    test_multiple_wakeups(true);
    test_user_exception();
    TEST_SUMMARY("Synchronization");
}

}  // namespace sync_test
