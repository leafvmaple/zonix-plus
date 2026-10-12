#include <sys/iterator.hpp>
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
#include <asm/fpu.h>
#include <asm/cr.h>
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

int init_page_protection() {
    constexpr uint32_t extended_features = 0x80000001U;
    constexpr uint32_t nx_feature = 1U << 20;
    uint32_t eax = 0x80000000U;
    uint32_t ebx{}, ecx{}, edx{};
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < extended_features) {
        return -1;
    }
    eax = extended_features;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if ((edx & nx_feature) == 0) {
        return -1;
    }
    asm volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(MSR_EFER));
    eax |= EFER_NXE;
    asm volatile("wrmsr" : : "a"(eax), "d"(edx), "c"(MSR_EFER) : "memory");
    uintptr_t cr0{};
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WP;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    return 0;
}

const InitStep ARCH_STEPS[] = {
    {"page_protect", init_page_protection, true},
    {"sse/sse2", fpu::init, true},
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
        *count = sys::size(ARCH_STEPS);
    }
    return ARCH_STEPS;
}

const InitStep* arch_pci_steps(size_t* count) {
    if (count != nullptr) {
        *count = sys::size(PCI_STEPS);
    }
    return PCI_STEPS;
}

// ============================================================================
// Runtime arch helpers
// ============================================================================

void arch_set_kernel_stack(uintptr_t kernel_stack_top_va) {
    tss::set_rsp0(kernel_stack_top_va);
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

void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry_va, uintptr_t fn, uintptr_t arg) {
    tf->cs = KERNEL_CS;
    tf->rflags = FL_IF;
    tf->rip = entry_va;
    tf->regs.rdi = fn;
    tf->regs.rsi = arg;
    tf->rsp = 0;
}

void arch_fixup_fork_tf(TrapFrame* tf, uintptr_t stack_va) {
    tf->set_return(0);  // Return 0 for child
    if (stack_va != 0) {
        tf->rsp = stack_va;
    } else {
        tf->rsp = reinterpret_cast<uintptr_t>(tf);
    }
    tf->rflags |= FL_IF;
    if (tf->cs == KERNEL_CS) {
        tf->ss = KERNEL_DS;
    }
}

void arch_setup_user_tf(TrapFrame* tf, uintptr_t entry_va, uintptr_t user_stack_va) {
    tf->cs = USER_CS;
    tf->ss = USER_DS;
    tf->rflags = FL_IF;
    tf->rip = entry_va;
    tf->rsp = user_stack_va;
}
