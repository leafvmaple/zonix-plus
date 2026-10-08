#include "test/test_defs.h"
#include "block/blk.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "sched/sched.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Helper: mount second disk at /mnt (userdata.img)
// The first disk is the boot disk; the second is userdata.
// ============================================================================

// Count attached Disk-type block devices.
static int count_disks() {
    int count = BlockManager::get_device_count();
    int disks = 0;
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (dev && dev->type == blk::DeviceType::Disk) {
            disks++;
        }
    }
    return disks;
}

static bool ensure_mnt_mounted() {
    if (vfs::is_mounted("/mnt")) {
        return true;
    }

    int count = BlockManager::get_device_count();
    int disk_index = 0;
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }
        // Skip the first disk (boot/system), mount the second
        if (disk_index++ < 1) {
            continue;
        }
        if (vfs::mount("/mnt", dev, "fat") == Error::None) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Test: exec a zcc-compiled user program from /mnt
// ============================================================================

static void test_exec_hello() {
    TEST_START("exec — run hello ELF from userdata disk");

    // Some targets (e.g. aarch64/riscv64 single-disk QEMU configs) have no
    // separate userdata disk. Skip the test in that case so CI still passes.
    if (count_disks() < 2) {
        cprintf("  [SKIP] no userdata disk attached\n");
        TEST_END();
        return;
    }

    bool mounted = ensure_mnt_mounted();
    TEST_ASSERT(mounted, "Userdata disk mounted at /mnt");
    if (!mounted) {
        TEST_END();
        return;
    }

    // Check the file exists
    vfs::File* probe = nullptr;
    const char* path = "/mnt/ZHELLO.ELF";
    Error rc = vfs::open(path, &probe);
    if (rc == Error::NotFound) {
        // The native assembly hello remains available without the zcc submodule.
        path = "/mnt/HELLO.ELF";
        rc = vfs::open(path, &probe);
    }
    TEST_ASSERT(rc == Error::None && probe != nullptr, "Hello ELF found on disk");
    if (probe) {
        vfs::close(probe);
    }
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Execute and wait for exit
    auto pid_r = exec::exec(path);
    TEST_ASSERT(pid_r.ok(), "exec() returned valid PID");
    if (!pid_r.ok()) {
        TEST_END();
        return;
    }

    int exit_code = -1;
    auto wait_r = sched::wait(pid_r.value(), &exit_code);
    TEST_ASSERT(wait_r.ok(), "sched::wait() succeeded");
    TEST_ASSERT(exit_code == 0, "User program exited with code 0");

    TEST_END();
}

// ============================================================================

namespace exec_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_exec_hello();

    TEST_SUMMARY("Exec (E2E)");
}

}  // namespace exec_test
