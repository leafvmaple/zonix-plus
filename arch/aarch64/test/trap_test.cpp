#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/page.h>
#include <asm/trap_numbers.h>
#include "trap/trap.h"
#include "test/test_defs.h"
#include "test/unit/trap/trap_test.h"
#include "drivers/intr.h"

namespace arch_trap_test {

void test() {
    int tests_passed{};
    int tests_failed{};
    intr::Guard guard;
    TEST_START("Architecture exception origin and interrupt classification");
    TrapFrame tf{};
    arch_setup_user_tf(&tf, 0x400000, USER_STACK_TOP);
    TEST_ASSERT(tf.from_user(), "User frame reports user origin");
    tf.vector = ARM64_TRAP_VECTOR_IRQ;
    tf.esr = static_cast<uint64_t>(TRAP_EC_SYSCALL) << 26;
    tf.regs[0] = 0x12345678;
    TEST_ASSERT(!trap::arch_is_syscall(&tf), "IRQ ignores stale SVC syndrome");
    TEST_ASSERT(trap::arch_try_handle_irq(&tf), "IRQ is handled from its vector tag");
    TEST_ASSERT(tf.pc == 0x400000 && tf.regs[0] == 0x12345678, "IRQ preserves interrupted PC and return register");
    tf.esr = static_cast<uint64_t>(TRAP_EC_PGFAULT_DATA_LOWER) << 26;
    TEST_ASSERT(!trap::arch_is_page_fault(&tf), "IRQ ignores stale data-abort syndrome");
    tf.vector = ARM64_TRAP_VECTOR_FIQ;
    TEST_ASSERT(!trap::arch_is_page_fault(&tf) && !trap::arch_is_syscall(&tf),
                "FIQ does not enter synchronous dispatch");
    tf.vector = ARM64_TRAP_VECTOR_SYNC;
    TEST_ASSERT(trap::arch_is_page_fault(&tf), "Synchronous data abort still classified");
    tf.esr = static_cast<uint64_t>(TRAP_EC_SYSCALL) << 26;
    TEST_ASSERT(trap::arch_is_syscall(&tf), "Synchronous SVC still classified");
    arch_setup_kthread_tf(&tf, 0x400000, 0, 0);
    TEST_ASSERT(!tf.from_user(), "Kernel frame reports kernel origin");
    TEST_END();
    TEST_SUMMARY("Architecture traps");
}

}  // namespace arch_trap_test
