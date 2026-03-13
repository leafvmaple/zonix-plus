/**
 * AArch64 architecture initialization and runtime abstractions.
 *
 * Minimal stubs so the kernel can link and boot to the serial console.
 * TODO: implement GIC, timer, exception vectors.
 */

#include <asm/arch.h>
#include <asm/trapframe.h>
#include <asm/memlayout.h>
#include <kernel/bootinfo.h>

// PL011 UART base address (QEMU virt machine)
static constexpr uintptr_t UART_PHYS = 0x09000000;

static volatile uint32_t* uart() {
    return phys_to_virt<uint32_t>(UART_PHYS);
}

// Minimal UART putc (PL011 data register at offset 0)
static void uart_putc(char c) {
    if (c == '\n')
        uart()[0] = '\r';
    uart()[0] = static_cast<uint32_t>(c);
}

static void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

// ============================================================================
// Architecture initialization
// ============================================================================

void arch_early_init() {
    // TODO: GIC init, timer init, exception vector table
    uart_puts("arch_early_init: aarch64 stub\n");
}

// ============================================================================
// Runtime arch helpers (stubs)
// ============================================================================

void arch_switch_rsp0(uintptr_t) {
    // TODO: update SP_EL0 or equivalent for current task
}

void arch_irq_eoi(int) {
    // TODO: GIC end-of-interrupt
}

void arch_setup_kthread_tf(TrapFrame* tf, uintptr_t entry, uintptr_t fn, uintptr_t arg) {
    // ELR_EL1 = entry, x0 = fn, x1 = arg
    tf->pc = entry;
    tf->regs[0] = fn;
    tf->regs[1] = arg;
    tf->pstate = 0x00000005;  // EL1h, IRQ masked
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
