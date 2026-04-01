/**
 * @file arch_init.cpp
 * @brief RISC-V architecture initialization and runtime abstractions.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/trap_numbers.h>
#include <asm/cpu.h>
#include <asm/page.h>
#include <kernel/bootinfo.h>

#include "drivers/plic.h"
#include "drivers/timer.h"
#include "drivers/uart16550.h"
#include "lib/stdio.h"
#include "lib/array.h"

/* kernel_trap_vec / user_trap_vec are in trapentry.S */
extern "C" char kernel_trap_vec[];

namespace {

static int trap_vectors_init() {
    /* Install kernel_trap_vec as stvec (mode=0 = direct, not vectored) */
    uintptr_t vec = reinterpret_cast<uintptr_t>(kernel_trap_vec);
    /* stvec must be 4-byte aligned; low 2 bits are mode (00=direct) */
    __asm__ volatile("csrw stvec, %0" : : "r"(vec & ~3UL) : "memory");
    return 0;
}

static int plic_init() {
    return plic::init();
}

static int timer_init() {
    return timer::init();
}

const InitStep ARCH_STEPS[] = {
    {"trap_vectors", trap_vectors_init, true},
    {"plic", plic_init, true},
    {"timer", timer_init, true},
};

/* No additional PCI-post-probe steps on bare RISC-V */
const InitStep* const PCI_STEPS = nullptr;
constexpr size_t PCI_STEPS_COUNT = 0;

}  // namespace

const InitStep* arch_early_steps(size_t* count) {
    if (count != nullptr) {
        *count = array_size(ARCH_STEPS);
    }
    return ARCH_STEPS;
}

const InitStep* arch_pci_steps(size_t* count) {
    if (count != nullptr) {
        *count = PCI_STEPS_COUNT;
    }
    return PCI_STEPS;
}

/*
 * arch_switch_rsp0 — update sscratch with the kernel stack top for
 * the next task.  Called by the scheduler before returning to user mode.
 * sscratch is used by user_trap_vec to find the kernel stack.
 */
void arch_switch_rsp0(uintptr_t sp0) {
    __asm__ volatile("csrw sscratch, %0" : : "r"(sp0) : "memory");
}

/*
 * arch_irq_eoi — signal end-of-interrupt to the PLIC.
 * Called after the IRQ handler has finished processing.
 */
void arch_irq_eoi(int irq) {
    if (irq > 0) {
        plic::complete(static_cast<uint32_t>(irq));
    }
}

/* Enable a specific IRQ line in the PLIC */
void arch_irq_enable_line(int irq) {
    plic::enable(irq);
}

/*
 * arch_pci_intx_to_irq — map a PCI INTx pin to the platform IRQ number.
 * QEMU virt RISC-V virtio devices use PLIC IRQs starting at 1.
 * The exact mapping depends on the virtio device slot; return 0 for unknown.
 */
int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin) {
    /* QEMU virt: virtio devices at slots 1..8 get IRQs 1..8 */
    if (dev >= 1 && dev <= 8 && int_pin == 1) {
        return static_cast<int>(dev);
    }
    return 0;
}

/*
 * arch_setup_kthread_tf — initialise TrapFrame for a new kernel thread.
 *
 * The thread "returns" from switch_to() to forkret → trapret → sret.
 * After sret:
 *   - CPU is in S-mode  (SSTATUS.SPP = 1)
 *   - IRQs are enabled  (SSTATUS.SPIE = 1)
 *   - PC  = entry (the thread body or a small wrapper)
 *   - a0  = fn   (kernel function to call)
 *   - a1  = arg  (argument for fn)
 */
void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg) {
    if (!tf) {
        return;
    }
    tf->sepc = entry;
    tf->sstatus = SSTATUS_SPP | SSTATUS_SPIE; /* S-mode, IRQs on after sret */
    tf->regs[10] = fn;                        /* a0 */
    tf->regs[11] = arg;                       /* a1 */
    /* TF_SP (kernel sp) is filled in by process creation (copy_thread) */
}

/*
 * arch_fixup_fork_tf — make the child task return 0 from fork()
 * and use the new user stack.
 */
void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp) {
    if (!tf) {
        return;
    }
    tf->regs[10] = 0; /* a0 = 0 (child returns 0 from fork) */
    tf->regs[2] = sp; /* sp = new user sp */
}

/*
 * arch_setup_user_tf — set up TrapFrame for first entry into user mode.
 *
 * After sret:
 *   - CPU is in U-mode  (SSTATUS.SPP = 0)
 *   - IRQs are enabled  (SSTATUS.SPIE = 1)
 *   - PC  = entry
 *   - sp  = usp  (user stack)
 */
void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp) {
    if (!tf) {
        return;
    }
    tf->sepc = entry;
    tf->sstatus = SSTATUS_SPIE; /* U-mode (SPP=0), IRQs enabled after sret */
    tf->regs[2] = usp;          /* x2 = sp */
}
