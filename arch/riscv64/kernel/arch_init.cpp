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

void arch_switch_rsp0(uintptr_t sp0) {
    __asm__ volatile("csrw sscratch, %0" : : "r"(sp0) : "memory");
}

void arch_irq_eoi(int irq) {
    if (irq > 0) {
        plic::complete(static_cast<uint32_t>(irq));
    }
}

void arch_irq_enable_line(int irq) {
    plic::enable(irq);
}

int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin) {
    /* QEMU virt: virtio devices at slots 1..8 get IRQs 1..8 */
    if (dev >= 1 && dev <= 8 && int_pin == 1) {
        return static_cast<int>(dev);
    }
    return 0;
}

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

void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t sp) {
    if (!tf) {
        return;
    }

    if ((tf->sstatus & SSTATUS_SPP) == 0) {
        tf->regs[10] = 0; /* a0 = 0 (user child returns 0 from fork) */
    }

    if (sp != 0) {
        tf->regs[2] = sp; /* explicit stack from caller */
    } else {
        tf->regs[2] = reinterpret_cast<uintptr_t>(tf); /* default to TF on kstack */
    }
}

void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp) {
    if (!tf) {
        return;
    }
    tf->sepc = entry;
    tf->sstatus = SSTATUS_SPIE; /* U-mode (SPP=0), IRQs enabled after sret */
    tf->regs[2] = usp;          /* x2 = sp */
}
