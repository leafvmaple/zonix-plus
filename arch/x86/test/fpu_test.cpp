#include <asm/cr.h>
#include <asm/fpu.h>
#include "block/blk.h"
#include "drivers/intr.h"
#include "drivers/i8253.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "sched/sched.h"
#include "test/test_defs.h"

namespace arch_trap_test {

void test_sse() {
    int tests_passed{};
    int tests_failed{};
    TEST_START("SSE/SSE2 initialization and user task isolation");
    uintptr_t cr0{}, cr4{};
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    TEST_ASSERT((cr0 & (CR0_EM | CR0_TS)) == 0 && (cr0 & (CR0_MP | CR0_NE)) == (CR0_MP | CR0_NE),
                "Native floating point enabled without lazy switching");
    TEST_ASSERT((cr4 & (CR4_OSFXSR | CR4_OSXMMEXCPT)) == (CR4_OSFXSR | CR4_OSXMMEXCPT) && (cr4 & CR4_OSXSAVE) == 0,
                "SSE exceptions enabled; unsupported extended state disabled");

    // Temporarily mount the userdata disk and release it before shared FS tests.
    // Missing disks are supported by kernel-only QEMU configurations.
    BlockDevice* userdata = nullptr;
    int disks{};
    for (int i = 0; i < BlockManager::device_count(); ++i) {
        auto* device = BlockManager::find_device(i);
        if (device && device->type == blk::DeviceType::Disk && ++disks == 2) {
            userdata = device;
            break;
        }
    }
    if (!userdata || vfs::is_mounted("/mnt")) {
        cprintf("  [SKIP] SSE user tests require an available userdata disk\n");
        TEST_END();
        TEST_SUMMARY("SSE/SSE2");
        return;
    }
    const Error mounted = vfs::mount("/mnt", userdata, "fat");
    TEST_ASSERT(mounted == Error::None, "Mounted SSE user test images");
    if (mounted == Error::None) {
        // The peers can finish while the parent is preempted, even before wait.
        // Record the timer baseline before publishing either runnable child.
        const auto start_ticks = timer::ticks;
        auto first = exec::exec("/mnt/SSETEST.ELF");
        auto second = exec::exec("/mnt/SSEPEER.ELF");
        {
            // Short quanta force timer preemption while the peers' patterns differ.
            intr::Guard guard;
            if (first.ok()) {
                auto* task = sched::find_process(first.value());
                task->priority = sched_prio::HIGHEST_PRIORITY;
                task->time_slice = 1;
            }
            if (second.ok()) {
                auto* task = sched::find_process(second.value());
                task->priority = sched_prio::HIGHEST_PRIORITY;
                task->time_slice = 1;
            }
        }
        TEST_ASSERT(first.ok() && second.ok(), "Started two SSE users with distinct register patterns");
        const Result<int>* pids[] = {&first, &second};
        for (const auto* pid : pids) {
            if (pid->ok()) {
                int code = -1;
                const auto waited = sched::wait(pid->value(), &code);
                TEST_ASSERT(waited.ok() && code == 0,
                            "User SSE/SSE2 arithmetic and XMM/x87/MXCSR survive IRQs and syscalls");
            }
        }
        TEST_ASSERT(timer::ticks - start_ticks >= 4, "User register checks span multiple timer quanta");
        TEST_ASSERT(vfs::umount("/mnt") == Error::None, "Released SSE test mount");
    }
    TEST_END();
    TEST_SUMMARY("SSE/SSE2");
}

}  // namespace arch_trap_test
