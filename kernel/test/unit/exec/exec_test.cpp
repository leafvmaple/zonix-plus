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

static void test_exec_zcc_hello() {
    TEST_START("exec — run ZHELLO.ELF from userdata disk");

    bool mounted = ensure_mnt_mounted();
    TEST_ASSERT(mounted, "Userdata disk mounted at /mnt");
    if (!mounted) {
        TEST_END();
        return;
    }

    // Check the file exists
    vfs::File* probe = nullptr;
    Error rc = vfs::open("/mnt/ZHELLO.ELF", &probe);
    TEST_ASSERT(rc == Error::None && probe != nullptr, "ZHELLO.ELF found on disk");
    if (probe) {
        vfs::close(probe);
    }
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Execute and wait for exit
    auto pid_r = exec::exec("/mnt/ZHELLO.ELF");
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

    test_exec_zcc_hello();

    TEST_SUMMARY("Exec (E2E)");
}

}  // namespace exec_test
