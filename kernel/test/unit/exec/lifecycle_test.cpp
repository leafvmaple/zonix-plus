#include "test/test_defs.h"
#include "test/test_main.h"
#include "exec/exec.h"
#include "sched/sched.h"
#include "mm/pmm.h"
#include "drivers/intr.h"
#include "fs/vfs.h"
#include <abi/process.h>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {
constexpr const char* PROGRAM = "/mnt/P1LIFE.ELF";

Result<sys::unique_ptr<exec::Arguments>> arguments(const char* mode = nullptr) {
    auto args = sys::unique_ptr<exec::Arguments>(new (sys::nothrow) exec::Arguments());
    ENSURE(args, Error::NoMem);
    TRY(args->add(PROGRAM));
    if (mode)
        TRY(args->add(mode));
    return args;
}

void test_user_lifecycle() {
    TEST_START("P1-1: real user parent spawns, shares FDs, handles errors and reaps 100 children");
    if (!exec_test::ensure_mnt_mounted()) {
        TEST_ASSERT(false, "User-program disk is available independently of the system disk");
        TEST_END();
        return;
    }
    auto prepared = arguments("short");
    TEST_ASSERT(prepared.ok(), "Prepared owned argv outside the kernel stack");
    if (!prepared.ok()) {
        TEST_END();
        return;
    }
    auto args = prepared.release_value();
    auto warmup = exec::spawn(PROGRAM, *args);
    TEST_ASSERT(warmup.ok(), "Loaded zcc user ELF from the independent user disk");
    if (!warmup.ok()) {
        cprintf("P1-1 load: %s\n", error_str(warmup.error()));
        TEST_END();
        return;
    }
    int code = -1;
    auto waited = sched::wait(warmup.value(), &code);
    TEST_ASSERT(waited.ok() && code == 7, "Initial user task returns through the common runtime");
    auto parent_args = arguments();
    TEST_ASSERT(parent_args.ok(), "Prepared parent argv");
    if (!parent_args.ok()) {
        TEST_END();
        return;
    }
    const size_t before_pages = pmm::free_page_count();
    const int before_tasks = TaskManager::process_count();
    const size_t before_fds = sched::current()->files().size();
    auto parent = exec::spawn(PROGRAM, *parent_args.value());
    TEST_ASSERT(parent.ok(), "Started real user-space lifecycle regression");
    if (parent.ok()) {
        code = -1;
        waited = sched::wait(parent.value(), &code);
        TEST_ASSERT(waited.ok() && waited.value() == parent.value() && code == 0,
                    "Arguments, shared offsets, invalid pointers, faults, kill and 100 wait cycles pass in user mode");
    }
    TEST_ASSERT(TaskManager::process_count() == before_tasks && sched::current()->files().size() == before_fds,
                "Reaping restores task and parent descriptor counts");
    TEST_ASSERT(pmm::free_page_count() == before_pages, "Address spaces, stacks and descriptions return all pages");
    TEST_ASSERT(vfs::unlink("/P1FD.TMP") == Error::None && vfs::unlink("/P1BAD.TMP") == Error::None,
                "Exited processes leave no file handles pinning their test artifacts");
    TEST_END();
}

void test_cancel_blocked_input() {
    TEST_START("P1-1: terminating a user task blocked on input unwinds its syscall before exit");
    auto args = arguments("read");
    TEST_ASSERT(args.ok(), "Prepared blocked-input child arguments");
    if (!args.ok()) {
        TEST_END();
        return;
    }
    auto child = exec::spawn(PROGRAM, *args.value());
    TEST_ASSERT(child.ok(), "Started user reader");
    if (!child.ok()) {
        TEST_END();
        return;
    }
    Task* task = sched::find_process(child.value());
    for (int i = 0; i < 1000 && task->state() != TaskState::Sleeping && task->state() != TaskState::Zombie; ++i)
        sched::schedule();
    TEST_ASSERT(task->state() == TaskState::Sleeping, "Reader reached an interruptible console wait");
    auto second = exec::spawn(PROGRAM, *args.value());
    TEST_ASSERT(second.ok(), "Started a second reader sharing the same open-file description");
    if (second.ok()) {
        Task* waiter = sched::find_process(second.value());
        for (int i = 0; i < 1000 && waiter->state() != TaskState::Sleeping && waiter->state() != TaskState::Zombie; ++i)
            sched::schedule();
        TEST_ASSERT(waiter->state() == TaskState::Sleeping, "Second reader waits for the shared file lock");
        TEST_ASSERT(TaskManager::terminate(second.value()) == Error::None,
                    "Parent can cancel a descriptor lock waiter");
        int second_code{};
        auto second_reaped = sched::wait(second.value(), &second_code);
        TEST_ASSERT(second_reaped.ok() && second_code == PROCESS_STATUS_KILLED,
                    "Lock cancellation does not require input or closing the first reader");
    }
    TEST_ASSERT(TaskManager::terminate(child.value()) == Error::None, "Parent can request child termination");
    int code{};
    auto reaped = sched::wait(child.value(), &code);
    TEST_ASSERT(reaped.ok() && code == PROCESS_STATUS_KILLED,
                "Cancellation wakes input and releases syscall-owned resources");
    TEST_ASSERT(TaskManager::terminate(TaskManager::idle_task()->pid) != Error::None,
                "User termination cannot kill a kernel task");
    TEST_END();
}
void test_pty_job_control() {
    TEST_START("P1-1: pseudo-terminal data path and foreground job control");
    auto args = arguments("pty");
    TEST_ASSERT(args.ok(), "Prepared pseudo-terminal driver arguments");
    if (!args.ok()) {
        TEST_END();
        return;
    }
    const size_t before_pages = pmm::free_page_count();
    const int before_tasks = TaskManager::process_count();
    auto driver = exec::spawn(PROGRAM, *args.value());
    TEST_ASSERT(driver.ok(), "Started the pseudo-terminal driver");
    if (driver.ok()) {
        int code = -1;
        auto waited = sched::wait(driver.value(), &code);
        if (code != 0)
            cprintf("P1-1 pty: driver exit code %d\n", code);
        TEST_ASSERT(waited.ok() && code == 0,
                    "Output, resize, Ctrl-C, cancel, input delivery and hangup behave in user mode");
    }
    TEST_ASSERT(TaskManager::process_count() == before_tasks, "The session leaves no tasks behind");
    TEST_ASSERT(pmm::free_page_count() == before_pages, "The session and its tasks return all pages");
    TEST_END();
}
}  // namespace

namespace lifecycle_test {
void test() {
    tests_passed = tests_failed = 0;
    test_user_lifecycle();
    test_cancel_blocked_input();
    test_pty_job_control();
    TEST_SUMMARY("P1-1 user lifecycle");
}
bool run_e2e() {
    auto marker = vfs::open("/P1.RUN");
    if (!marker.ok())
        return false;
    marker.release_value().reset();
    test();
    return true;
}
}  // namespace lifecycle_test
