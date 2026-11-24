#include "test_main.h"
#include "test/unit/mm/swap_test.h"
#include "test/unit/trap/trap_test.h"
#include "lib/stdio.h"

#include <base/types.h>
#include <asm/arch.h>

namespace sched {
void test();
}

namespace string_test {
void test();
}

namespace list_test {
void test();
}

namespace result_test {
void test();
}

namespace ownership_test {
void test();
}

namespace pmm_test {
void test();
}

namespace blk_test {
void test();
}

namespace elf_test {
void test();
}

namespace shell_test {
void test();
}

namespace fat_validation_test {
void test();
}

namespace file_io_test {
void test();
}

namespace storage_sync_test {
void test();
bool run_power_e2e();
}  // namespace storage_sync_test

namespace fs_test {
void test();
}

namespace pipe_test {
void test();
}

namespace lifecycle_test {
void test();
bool run_e2e();
}  // namespace lifecycle_test

namespace exec_test {
void test();
}

namespace stack_overflow_e2e {
bool run();
}

namespace vmm_test {
void test();
}

namespace sync_test {
void test();
}

namespace log_test {
void test();
}

// QEMU ISA debug exit port (configured via -device isa-debug-exit,iobase=0xf4,iosize=0x04)
static constexpr uint16_t QEMU_EXIT_PORT = 0xf4;

static void run_swap_suite() {
    run_swap_tests();
}

struct TestSuite {
    const char* name;
    void (*run)();
};

static const TestSuite SUITES[] = {
    {"Architecture traps", arch_trap_test::test},
    {"Synchronization", sync_test::test},
    {"User Memory", vmm_test::test},
    {"String Library", string_test::test},
    {"Linked List", list_test::test},
    {"Result Library", result_test::test},
    {"Resource Ownership", ownership_test::test},
    {"PMM Allocator", pmm_test::test},
    {"Scheduler", sched::test},
    {"Swap (FIFO)", run_swap_suite},
    {"Block Manager", blk_test::test},
    {"ELF Loader", elf_test::test},
    {"FAT validation", fat_validation_test::test},
    {"P0-1 file I/O", file_io_test::test},
    {"P0-2 storage sync", storage_sync_test::test},
    {"File System", fs_test::test},
    {"Pipes", pipe_test::test},
    {"Kernel log", log_test::test},
    {"Shell", shell_test::test},
    {"Exec (E2E)", exec_test::test},
    {"P1-1 user lifecycle", lifecycle_test::test},
};

int test_run_all(void*) {
    cprintf("\n");
    cprintf("########################################\n");
    cprintf("#     ZONIX OS — CI TEST SUITE         #\n");
    cprintf("########################################\n");

    int suite_count = sizeof(SUITES) / sizeof(SUITES[0]);

    // Dedicated reboot fixtures do not repeat the independent unit suites.
    if (!stack_overflow_e2e::run() && !storage_sync_test::run_power_e2e() && !lifecycle_test::run_e2e()) {
        for (int i = 0; i < suite_count; i++) {
            cprintf("\n>>> [%d/%d] Running: %s\n", i + 1, suite_count, SUITES[i].name);
            SUITES[i].run();
        }
    }

    cprintf("\n########################################\n");
    cprintf("#     CI_TEST_COMPLETE                 #\n");
    cprintf("########################################\n");

    // Exit QEMU via ISA debug exit device.
    // Writing value V causes exit code (V << 1) | 1.
    // V=0 → exit code 1 (we treat as success in CI script).
    arch_port_write8(QEMU_EXIT_PORT, 0);

    arch_halt();
    return 0;
}
