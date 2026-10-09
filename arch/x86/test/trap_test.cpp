#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/page.h>
#include <asm/trap_numbers.h>
#include "trap/trap.h"
#include "test/test_defs.h"
#include "test/unit/trap/trap_test.h"
#include "drivers/intr.h"

namespace arch_trap_test {

void test_sse();

static void test_exception_classification() {
    int tests_passed{};
    int tests_failed{};
    intr::Guard guard;
    TEST_START("Architecture exception origin and interrupt classification");
    TrapFrame tf{};
    arch_setup_user_tf(&tf, 0x400000, USER_STACK_TOP);
    TEST_ASSERT(tf.from_user(), "User frame reports user origin");
    tf.trapno = T_SYSCALL;
    TEST_ASSERT(trap::arch_is_syscall(&tf), "User syscall still classified");
    tf.trapno = IRQ_OFFSET;
    TEST_ASSERT(!trap::arch_is_syscall(&tf) && !trap::arch_is_page_fault(&tf),
                "IRQ does not enter synchronous dispatch");
    GateDesc gate{};
    set_interrupt_gate(&gate, 0x400000);
    TEST_ASSERT((gate.gd_type_attr & GATE_TYPE_MASK) == STS_IG32 &&
                    (gate.gd_type_attr & GATE_DPL_MASK) == (DPL_KERNEL << GATE_DPL_SHIFT),
                "Kernel gate disables interrupts on entry");
    set_sys_gate(&gate, 0x400000);
    TEST_ASSERT((gate.gd_type_attr & GATE_TYPE_MASK) == STS_IG32 &&
                    (gate.gd_type_attr & GATE_DPL_MASK) == (DPL_USER << GATE_DPL_SHIFT),
                "User syscall gate disables interrupts and keeps user DPL");
    arch_setup_kthread_tf(&tf, 0x400000, 0, 0);
    TEST_ASSERT(!tf.from_user(), "Kernel frame reports kernel origin");
    TEST_END();
    TEST_SUMMARY("Architecture traps");
}

void test() {
    test_exception_classification();
    test_sse();
}

}  // namespace arch_trap_test
