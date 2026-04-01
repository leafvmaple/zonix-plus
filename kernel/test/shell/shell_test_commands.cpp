#include "cons/shell.h"
#include "block/blk.h"
#include "test/unit/mm/swap_test.h"

#include "lib/stdio.h"

namespace sched {
void test();
}

[[gnu::weak]] extern void driver_test_disktest();
[[gnu::weak]] extern void driver_test_intrtest();

namespace {

static void run_generic_disktest() {
    int count = BlockManager::get_device_count();
    if (count == 0) {
        cprintf("No block devices found\n");
        return;
    }

    cprintf("\n=== Generic Disk Read Test ===\n");
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }

        uint8_t buf[BlockDevice::SIZE] = {};
        int rc = dev->read(0, buf, 1);
        cprintf("  %s: read sector 0 %s\n", dev->name, rc == 0 ? "OK" : "FAILED");
    }
    cprintf("=== Test Complete ===\n\n");
}

static void run_generic_intrtest() {
    int count = BlockManager::get_device_count();
    if (count == 0) {
        cprintf("No block devices found\n");
        return;
    }

    cprintf("\n=== Generic Interrupt-Path Test ===\n");
    cprintf("Reading sector 0 to exercise each driver's normal I/O completion path.\n");

    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::get_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }

        uint8_t buf[BlockDevice::SIZE] = {};
        int rc = dev->read(0, buf, 1);
        cprintf("  %s: completion path %s\n", dev->name, rc == 0 ? "OK" : "FAILED");
    }
    cprintf("=== Test Complete ===\n\n");
}

void cmd_swaptest(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    run_swap_tests();
}

void cmd_disktest(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    cprintf("Running disk test...\n");
    if (driver_test_disktest) {
        driver_test_disktest();
        return;
    }

    run_generic_disktest();
}

void cmd_intrtest(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    cprintf("Running interrupt test...\n");
    if (driver_test_intrtest) {
        driver_test_intrtest();
        return;
    }

    run_generic_intrtest();
}

void cmd_schedtest(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);
    sched::test();
}

void register_test_command(const char* name, const char* desc, shell::fnCommand func) {
    if (shell::register_command(name, desc, func) != 0) {
        cprintf("shell test ext: failed to register '%s'\n", name);
    }
}

}  // namespace

void shell_register_extensions() {
    register_test_command("swaptest", "Run swap system tests", cmd_swaptest);
    register_test_command("disktest", "Test disk read/write", cmd_disktest);
    register_test_command("intrtest", "Test block device interrupts", cmd_intrtest);
    register_test_command("schedtest", "Run scheduler unit tests", cmd_schedtest);
}
