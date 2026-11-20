#include "drivers/intr.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "sched/sched.h"
#include "test/test_defs.h"
#include "test/test_main.h"
#include "trap/trap.h"

namespace arch_trap_test {

// Two user programs fill every vector register, FPCR and FPSR with different
// values and check them while timer preemption and syscalls switch between
// them. Each exits with 0 only if its own state survived.
void test_fp_simd() {
    int tests_passed{};
    int tests_failed{};
    TEST_START("FP/SIMD user task isolation");
    // Leave /mnt as it was found. Kernel-only QEMU configurations have no
    // userdata disk.
    const bool mounted_here = !vfs::is_mounted("/mnt");
    if (!exec_test::ensure_mnt_mounted()) {
        cprintf("  [SKIP] FP/SIMD user tests require an available userdata disk\n");
        TEST_END();
        TEST_SUMMARY("FP/SIMD");
        return;
    }
    // The peers can finish while the parent is preempted, even before wait.
    // Record the timer baseline before publishing either runnable child.
    const auto start_ticks = trap::Clock::ticks();
    auto first = exec::exec("/mnt/FPTEST.ELF");
    auto second = exec::exec("/mnt/FPPEER.ELF");
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
    TEST_ASSERT(first.ok() && second.ok(), "Started two FP/SIMD users with distinct register patterns");
    for (const auto* pid : pids) {
        if (pid->ok()) {
            int code = -1;
            const auto waited = sched::wait(pid->value(), &code);
            TEST_ASSERT(waited.ok() && code == 0, "User q0-q31, FPCR and FPSR survive IRQs and syscalls");
        }
    }
    TEST_ASSERT(trap::Clock::ticks() - start_ticks >= 4, "User register checks span multiple timer quanta");
    if (mounted_here) {
        TEST_ASSERT(vfs::umount("/mnt") == Error::None, "Released FP/SIMD test mount");
    }
    TEST_END();
    TEST_SUMMARY("FP/SIMD");
}

}  // namespace arch_trap_test
