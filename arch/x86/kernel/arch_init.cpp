/**
 * x86_64 architecture initialization and runtime abstractions.
 *
 * Implements the arch_*() functions declared in <asm/arch.h> that
 * cannot be simple inline wrappers.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/segments.h>
#include <asm/cpu.h>
#include <base/types.h>

#include "idt.h"
#include "tss.h"
#include "drivers/ahci.h"
#include "drivers/i8259.h"
#include "drivers/i8253.h"

// ============================================================================
// Architecture initialization
// ============================================================================

namespace {

const InitStep ARCH_STEPS[] = {
    {"i8259", i8259::init, true},
    {"i8253", i8253::init, true},
    {"idt", idt::init, true},
    {"tss", tss::init, true},
};

const InitStep PCI_STEPS[] = {
    {"ahci", AhciManager::init, false},
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

// ============================================================================
// Runtime arch helpers
// ============================================================================

void arch_switch_rsp0(uintptr_t rsp0) {
    tss::set_rsp0(rsp0);
}

void arch_irq_eoi(int irq) {
    i8259::send_eoi(irq);
}

void arch_irq_enable_line(int irq) {
    i8259::enable(static_cast<unsigned int>(irq));
}

int arch_pci_intx_to_irq(uint8_t dev, uint8_t int_pin) {
    // x86: read interrupt line from PCI config (set by BIOS/firmware)
    // The int_pin parameter is unused; IRQ is pre-assigned via PCI_INTERRUPT_LINE.
    // Caller should pass the value from config offset 0x3C bits[7:0].
    (void)dev;
    (void)int_pin;
    return -1;  // x86 callers read IRQ line directly from PCI config
}

void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg) {
    tf->cs = KERNEL_CS;
    tf->rflags = FL_IF;
    tf->rip = entry;
    tf->regs.rdi = fn;
    tf->regs.rsi = arg;
    tf->rsp = 0;
}

void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t esp) {
    tf->set_return(0);  // Return 0 for child
    if (esp != 0) {
        tf->rsp = esp;
    } else {
        tf->rsp = reinterpret_cast<uintptr_t>(tf);
    }
    tf->rflags |= FL_IF;
    if (tf->cs == KERNEL_CS) {
        tf->ss = KERNEL_DS;
    }
}

void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry, uintptr_t usp) {
    tf->cs = USER_CS;
    tf->ss = USER_DS;
    tf->rflags = FL_IF;
    tf->rip = entry;
    tf->rsp = usp;
}
