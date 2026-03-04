#include "trap.h"

#include "lib/unistd.h"
#include <base/types.h>
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/drivers/i8259.h>

#include "drivers/kbd.h"
#include "drivers/pit.h"
#include "drivers/pic.h"
#include "drivers/ide.h"
#include "drivers/fbcons.h"
#include "cons/cons.h"
#include "mm/vmm.h"
#include "sched/sched.h"

namespace {

constexpr int TICK_NUM = 100;

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

const char* trap_name(int trapno) {
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
    cprintf("Page Fault at 0x%016lx: %c/%c [%s].\n", arch_read_cr2(), (err & 4) ? 'U' : 'K', (err & 2) ? 'W' : 'R',
            (err & 1) ? "Protection Fault" : "No Page Found");
}

static void irq_timer(TrapFrame* tf) {
    pit::ticks++;
    if (static_cast<int>(pit::ticks) % TICK_NUM == 0) {
        // cprintf("%d ticks\n", TICK_NUM);
    }
    fbcons::tick();
}

static void irq_kbd(TrapFrame* tf) {
    kbd::intr();
}

static int pg_fault(TrapFrame* tf) {
    tf->print();
    tf->print_pgfault();

    TaskStruct* current = TaskManager::get_current();
    vmm::pg_fault(current->memory, tf->err, arch_read_cr2());

    return 0;
}

void trap(TrapFrame* tf) {
    switch (tf->trapno) {
        case T_PGFLT: pg_fault(tf); break;
        case IRQ_OFFSET + IRQ_TIMER: irq_timer(tf); break;
        case IRQ_OFFSET + IRQ_KBD: irq_kbd(tf); break;
        case IRQ_OFFSET + IRQ_IDE1:
            IdeManager::interrupt_handler(0);  // Primary channel
            break;
        case IRQ_OFFSET + IRQ_IDE2:
            IdeManager::interrupt_handler(1);  // Secondary channel
            break;
        case T_SYSCALL:
        default: break;
    }

    // Send EOI for hardware interrupts (IRQ 0-15)
    if (tf->trapno >= IRQ_OFFSET && tf->trapno < IRQ_OFFSET + 16) {
        pic::send_eoi(tf->trapno - IRQ_OFFSET);
    }
}