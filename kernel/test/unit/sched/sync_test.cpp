#include "test/test_defs.h"
#include "sched/sched.h"
#include "lib/semaphore.h"
#include "lib/mutex.h"

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
    TEST_SUMMARY("Synchronization");
}

}  // namespace sync_test
