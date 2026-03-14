/**
 * AArch64 architecture initialization and runtime abstractions.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/memlayout.h>
#include <kernel/bootinfo.h>

// Exception vector table (defined in trapentry.S)
extern "C" char _vectors[];

// ============================================================================
// Architecture initialization
// ============================================================================

static void timer_init() {
    // ARM Generic Timer: configure virtual timer for ~100 Hz
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t interval = freq / 100;  // 10ms per tick
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(interval));
    // Enable the virtual timer, unmask interrupt
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));
}

void arch_early_init() {
    // Install exception vector table
    uint64_t vbar = reinterpret_cast<uint64_t>(_vectors);
    __asm__ volatile("msr vbar_el1, %0" ::"r"(vbar));
    __asm__ volatile("isb");

    // Initialize timer (GIC configuration is not needed for QEMU virt
    // because the virtual timer fires as an EL1 IRQ automatically)
    timer_init();
}

// ============================================================================
// Runtime arch helpers
// ============================================================================

void arch_switch_rsp0(uintptr_t) {
    // AArch64 EL1 uses SP_EL1 implicitly; no TSS equivalent needed.
    // SP_EL0 is saved/restored by trap entry/exit.
}

void arch_irq_eoi(int) {
    // QEMU virt with virtual timer doesn't require GIC EOI;
    // the timer interrupt is auto-cleared when we write TVAL.
}

void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg) {
    // ELR_EL1 = entry (kernel_thread_entry), x0 = fn, x1 = arg
    tf->pc = entry;
    tf->regs[0] = fn;
    tf->regs[1] = arg;
    tf->pstate = 0x00000005;  // EL1h, IRQ unmasked (DAIF.I=0)
    tf->sp = 0;
}

void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp) {
    tf->set_return(0);
    if (sp != 0) {
        tf->sp = sp;
    } else {
        tf->sp = reinterpret_cast<uintptr_t>(tf);
    }
    tf->pstate &= ~(1 << 7);  // unmask IRQ
}

void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp) {
    tf->pc = entry;
    tf->sp = usp;
    tf->pstate = 0x00000000;  // EL0t, all exceptions unmasked
}
