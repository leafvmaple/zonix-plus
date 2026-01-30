#include "trap.h"

#include "unistd.h"
#include <base/types.h>
#include "stdio.h"

#include <arch/x86/io.h>
#include <arch/x86/drivers/i8259.h>

#include "../drivers/kdb.h"
#include "../drivers/pit.h"
#include "../drivers/pic.h"
#include "../drivers/hd.h"
#include "../cons/cons.h"
#include "../mm/vmm.h"
#include "../sched/sched.h"

namespace {

constexpr int TICK_NUM = 100;

const char* const excnames[] = {
    "Divide error",
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
    "SIMD Floating-Point Exception"
};

constexpr size_t NUM_EXCEPTIONS = sizeof(excnames) / sizeof(excnames[0]);

const char* trap_name(int trapno) {
    if (static_cast<size_t>(trapno) < NUM_EXCEPTIONS) {
        return excnames[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

} // namespace

void TrapRegisters::print() const {
    cprintf("  edi  0x%08x\n", m_edi);
    cprintf("  esi  0x%08x\n", m_esi);
    cprintf("  ebp  0x%08x\n", m_ebp);
    cprintf("  ebx  0x%08x\n", m_ebx);
    cprintf("  edx  0x%08x\n", m_edx);
    cprintf("  ecx  0x%08x\n", m_ecx);
    cprintf("  eax  0x%08x\n", m_eax);
}


void TrapFrame::print() const {
    cprintf("trapframe at %p\n", this);
    m_regs.print();
    cprintf("  trap 0x%08x %s\n", m_trapno, trap_name(m_trapno));
    cprintf("  err  0x%08x\n", m_err);
    cprintf("  eip  0x%08x\n", m_eip);
}

void TrapFrame::print_pgfault() const {
    cprintf("Page Fault at 0x%08x: %c/%c [%s].\n", rcr2(),
            (m_err & 4) ? 'U' : 'K',
            (m_err & 2) ? 'W' : 'R',
            (m_err & 1) ? "Protection Fault" : "No Page Found");
}

static void irq_timer(TrapFrame *tf) {
    ticks++;
    if ((int)ticks % TICK_NUM == 0) {
        // cprintf("%d ticks\n", TICK_NUM);
    }
}

static void irq_kbd(TrapFrame *tf) {
    extern void shell_handle_char(char c);
    char c = kdb_getc();
    if (c > 0) {
        shell_handle_char(c);
    }
}

static int pg_fault(TrapFrame *tf) {
    tf->print();
    tf->print_pgfault();

    vmm_pg_fault(current->mm, tf->m_err, rcr2());

    return 0;
}

void trap(TrapFrame *tf) {
    switch(tf->m_trapno) {
        case T_PGFLT:
            pg_fault(tf);
            break;
        case IRQ_OFFSET + IRQ_TIMER:
            irq_timer(tf);
            break;
        case IRQ_OFFSET + IRQ_KBD:
            irq_kbd(tf);
            break;
        case IRQ_OFFSET + IRQ_IDE1:
            hd_intr(IRQ_IDE1);
            break;
        case IRQ_OFFSET + IRQ_IDE2:
            hd_intr(IRQ_IDE2);
            break;
        case T_SYSCALL:
            break;
        default:
            break;
    }
    
    // Send EOI for hardware interrupts (IRQ 0-15)
    if (tf->m_trapno >= IRQ_OFFSET && tf->m_trapno < IRQ_OFFSET + 16) {
        pic_send_eoi(tf->m_trapno - IRQ_OFFSET);
    }
}