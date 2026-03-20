#include "test_main.h"
#include "test/unit/mm/swap_test.h"
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

// QEMU ISA debug exit port (configured via -device isa-debug-exit,iobase=0xf4,iosize=0x04)
static constexpr uint16_t QEMU_EXIT_PORT = 0xf4;

static void run_swap_suite() {
    run_swap_tests();
}

struct TestSuite {
    const char* name;
    void (*run)();
};

static const TestSuite suites[] = {
    {"String Library", string_test::test},
    {"Linked List", list_test::test},
    {"PMM Allocator", pmm_test::test},
    {"Scheduler", sched::test},
    {"Swap (FIFO)", run_swap_suite},
    {"Block Manager", blk_test::test},
    {"ELF Loader", elf_test::test},
    {"Shell", shell_test::test},
};

int test_run_all(void*) {
    cprintf("\n");
    cprintf("########################################\n");
    cprintf("#     ZONIX OS — CI TEST SUITE         #\n");
    cprintf("########################################\n");

    int suite_count = sizeof(suites) / sizeof(suites[0]);

    for (int i = 0; i < suite_count; i++) {
        cprintf("\n>>> [%d/%d] Running: %s\n", i + 1, suite_count, suites[i].name);
        suites[i].run();
    }

    cprintf("\n########################################\n");
    cprintf("#     CI_TEST_COMPLETE                 #\n");
    cprintf("########################################\n");

    // Exit QEMU via ISA debug exit device.
    // Writing value V causes exit code (V << 1) | 1.
    // V=0 → exit code 1 (we treat as success in CI script).
    arch_port_outb(QEMU_EXIT_PORT, 0);

    arch_halt();
    return 0;
}
