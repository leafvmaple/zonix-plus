#include "test/unit/sched/register_peers_test.h"
#include "drivers/intr.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "sched/sched.h"
#include "test/test_defs.h"
#include "test/test_main.h"
#include "trap/trap.h"

namespace register_peers_test {

void run(const char* title, const char* first_path, const char* second_path, const char* survived) {
    int tests_passed{};
    int tests_failed{};
    TEST_START(title);
    // Leave /mnt as it was found. Kernel-only QEMU configurations have no
    // userdata disk.
    const bool mounted_here = !vfs::is_mounted("/mnt");
    if (!exec_test::ensure_mnt_mounted()) {
        cprintf("  [SKIP] register isolation tests require an available userdata disk\n");
        TEST_END();
        TEST_SUMMARY(title);
        return;
    }
    // The peers can finish while the parent is preempted, even before wait.
    // Record the timer baseline before publishing either runnable child.
    const auto start_ticks = trap::Clock::ticks();
    auto first = exec::exec(first_path);
    auto second = exec::exec(second_path);
    const Result<int>* pids[] = {&first, &second};
    {
        // Short quanta force timer preemption while the peers' patterns differ.
        intr::Guard guard;
        for (const auto* pid : pids) {
            if (pid->ok()) {
                auto* task = sched::find_process(pid->value());
                task->priority = sched_prio::HIGHEST_PRIORITY;
                task->time_slice = 1;
            }
        }
    }
    TEST_ASSERT(first.ok() && second.ok(), "Started two users with distinct register patterns");
    for (const auto* pid : pids) {
        if (pid->ok()) {
            int code = -1;
            const auto waited = sched::wait(pid->value(), &code);
            TEST_ASSERT(waited.ok() && code == 0, survived);
        }
    }
    TEST_ASSERT(trap::Clock::ticks() - start_ticks >= 4, "User register checks span multiple timer quanta");
    if (mounted_here) {
        TEST_ASSERT(vfs::umount("/mnt") == Error::None, "Released register test mount");
    }
    TEST_END();
    TEST_SUMMARY(title);
}

}  // namespace register_peers_test
