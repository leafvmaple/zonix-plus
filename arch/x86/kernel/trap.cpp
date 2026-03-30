#include "trap/trap.h"

#include <base/types.h>
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/trap_numbers.h>

#include "drivers/i8042.h"
#include "drivers/ide.h"

namespace {

const char* const EXC_NAMES[] = {"Divide error",
                                 "Debug",
                                 "Non-Maskable Interrupt",
                                 "Breakpoint",
                                 "Overflow",
                                 "BOUND Range Exceeded",
                                 "Invalid Opcode",
                                 "Device Not Available",
                                 "Double Fault",
                                 "Coprocessor Segment Overrun",
                                 "Invalid TSS",
                                 "Segment Not Present",
                                 "Stack Fault",
                                 "General Protection",
                                 "Page Fault",
                                 "(unknown trap)",
                                 "x87 FPU Floating-Point Error",
                                 "Alignment Check",
                                 "Machine-Check",
                                 "SIMD Floating-Point Exception"};

constexpr size_t NUM_EXCEPTIONS = array_size(EXC_NAMES);

static const char* trap_name(int trapno) {
    if (static_cast<size_t>(trapno) < NUM_EXCEPTIONS) {
        return EXC_NAMES[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

}  // namespace

void TrapRegisters::print() const {
    cprintf("  rax  0x%016lx\n", rax);
    cprintf("  rcx  0x%016lx\n", rcx);
    cprintf("  rdx  0x%016lx\n", rdx);
    cprintf("  rbx  0x%016lx\n", rbx);
    cprintf("  rbp  0x%016lx\n", rbp);
    cprintf("  rsi  0x%016lx\n", rsi);
    cprintf("  rdi  0x%016lx\n", rdi);
    cprintf("  r8   0x%016lx\n", r8);
    cprintf("  r9   0x%016lx\n", r9);
    cprintf("  r10  0x%016lx\n", r10);
    cprintf("  r11  0x%016lx\n", r11);
    cprintf("  r12  0x%016lx\n", r12);
    cprintf("  r13  0x%016lx\n", r13);
    cprintf("  r14  0x%016lx\n", r14);
    cprintf("  r15  0x%016lx\n", r15);
}


void TrapFrame::print() const {
    cprintf("trapframe at %p\n", this);
    regs.print();
    cprintf("  trap 0x%08x %s\n", trapno, trap_name(trapno));
    cprintf("  err  0x%016lx\n", err);
    cprintf("  rip  0x%016lx\n", rip);
}

void TrapFrame::print_pgfault() const {
    cprintf("Page Fault at 0x%016lx: %c/%c [%s].\n", arch_fault_addr(), (err & 4) ? 'U' : 'K', (err & 2) ? 'W' : 'R',
            (err & 1) ? "Protection Fault" : "No Page Found");
}

namespace trap {

bool arch_try_handle_irq(TrapFrame* tf) {
    if (!tf) {
        return false;
    }

    if (tf->trapno < IRQ_OFFSET || tf->trapno >= IRQ_OFFSET + IRQ_COUNT) {
        return false;
    }

    switch (tf->trapno) {
        case TRAP_VECTOR_IRQ_TIMER: trap::handle_timer_tick(); break;
        case TRAP_VECTOR_IRQ_KBD: i8042::intr(); break;
        case TRAP_VECTOR_IRQ_IDE1: IdeManager::interrupt_handler(0); break;
        case TRAP_VECTOR_IRQ_IDE2: IdeManager::interrupt_handler(1); break;
        default: break;
    }

    return true;
}

bool arch_is_page_fault(const TrapFrame* tf) {
    return (tf != nullptr) && (tf->trapno == TRAP_VECTOR_PGFAULT);
}

uint32_t arch_page_fault_error(const TrapFrame* tf) {
    return tf ? static_cast<uint32_t>(tf->err) : 0;
}

uintptr_t arch_page_fault_addr(const TrapFrame* tf) {
    static_cast<void>(tf);
    return arch_fault_addr();
}

bool arch_is_syscall(const TrapFrame* tf) {
    return (tf != nullptr) && (tf->trapno == TRAP_VECTOR_SYSCALL);
}

void arch_on_syscall_entry(TrapFrame* tf) {
    static_cast<void>(tf);
}

void arch_on_unhandled(TrapFrame* tf) {
    static_cast<void>(tf);
}

void arch_post_dispatch(TrapFrame* tf) {
    if (!tf) {
        return;
    }

    if (tf->trapno >= IRQ_OFFSET && tf->trapno < IRQ_OFFSET + IRQ_COUNT) {
        arch_irq_eoi(tf->trapno - IRQ_OFFSET);
    }
}

}  // namespace trap