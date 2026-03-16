/**
 * AArch64 architecture initialization and runtime abstractions.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/memlayout.h>
#include <kernel/bootinfo.h>

// Exception vector table (defined in trapentry.S)
extern "C" char _vectors[];

namespace {

constexpr int ARCH_INIT_OK = 0;
constexpr int ARCH_INIT_ERR = -1;

static int vectors_init() {
    uint64_t vbar = reinterpret_cast<uint64_t>(_vectors);
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vbar));
    __asm__ volatile("isb");
    return ARCH_INIT_OK;
}

static int timer_init() {
    // ARM Generic Timer: configure virtual timer for ~100 Hz
    uint64_t freq{};
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) {
        return ARCH_INIT_ERR;
    }
    uint64_t interval = freq / 100;  // 10ms per tick
    if (interval == 0) {
        return ARCH_INIT_ERR;
    }
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(interval));
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));
    return ARCH_INIT_OK;
}

const InitStep ARCH_STEPS[] = {
    {"vectors", vectors_init, true},
    {"timer", timer_init, true},
};

}  // namespace

const InitStep* arch_early_steps(size_t* count) {
    if (count != nullptr) {
        *count = sizeof(ARCH_STEPS) / sizeof(ARCH_STEPS[0]);
    }
    return ARCH_STEPS;
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
