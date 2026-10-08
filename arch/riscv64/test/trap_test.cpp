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
    tf.scause = IRQ_SUPERVISOR_TIMER;
    TEST_ASSERT(!trap::arch_is_page_fault(&tf) && !trap::arch_is_syscall(&tf), "Interrupt cause cannot enter synchronous dispatch");
    tf.scause = CAUSE_USER_ECALL;
    TEST_ASSERT(trap::arch_is_syscall(&tf), "User ecall still classified");
    tf.scause = CAUSE_ILLEGAL_INSN;
    TEST_ASSERT(!trap::arch_is_syscall(&tf) && !trap::arch_is_page_fault(&tf), "Illegal instruction reaches unhandled dispatch");
    uintptr_t satp{};
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    TEST_ASSERT((satp & SATP_MODE_MASK) == SATP_SV39, "MMU remains in Sv39 mode");
    TEST_ASSERT(arch_read_page_table_root() == ((satp & SATP_PPN_MASK) << PG_SHIFT), "Root accessor decodes PPN into physical bytes");
    arch_setup_kthread_tf(&tf, 0x400000, 0, 0);
    TEST_ASSERT(!tf.from_user(), "Kernel frame reports kernel origin");
    TEST_END();
    TEST_SUMMARY("Architecture traps");
}

}  // namespace arch_trap_test
