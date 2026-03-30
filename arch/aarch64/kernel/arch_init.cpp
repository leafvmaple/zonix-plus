/**
 * AArch64 architecture initialization and runtime abstractions.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/memlayout.h>
#include <kernel/bootinfo.h>

#include "drivers/gic.h"
#include "drivers/timer.h"
#include "drivers/virtio_kbd.h"
#include "lib/stdio.h"

// Exception vector table (defined in trapentry.S)
extern "C" char _vectors[];

namespace {

static int vectors_init() {
    uint64_t vbar = reinterpret_cast<uint64_t>(_vectors);
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vbar));
    __asm__ volatile("isb");
    return 0;
}

const InitStep ARCH_STEPS[] = {
    {"vectors", vectors_init, true},
    {"gic", gic::init, true},
    {"timer", timer::init, true},
};

const InitStep PCI_STEPS[] = {
    {"virtio_kbd", virtio_kbd::init, false},
};

}  // namespace


const InitStep* arch_early_steps(size_t* count) {
    if (count != nullptr) {
        *count = array_size(ARCH_STEPS);
    }
    return ARCH_STEPS;
}

const InitStep* arch_pci_steps(size_t* count) {
    if (count != nullptr) {
        *count = array_size(PCI_STEPS);
    }
    return PCI_STEPS;
}


void arch_switch_rsp0(uintptr_t) {
    // AArch64 EL1 uses SP_EL1 implicitly; no TSS equivalent needed.
    // SP_EL0 is saved/restored by trap entry/exit.
}

void arch_irq_eoi(int irq) {
    gic::send_eoi(static_cast<uint32_t>(irq));
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
    // For user fork (EL0): child returns 0 in x0
    // For kernel thread (EL1): x0 holds fn pointer, must not overwrite
    if ((tf->pstate & 0x4) == 0) {
        tf->set_return(0);
    }
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
