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

#define TICK_NUM 100

static const char *trap_name(int trapno) {
    static const char *const excnames[] = {
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

    if (trapno < sizeof(excnames) / sizeof(const char *const)) {
        return excnames[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

void print_regs(trap_regs *regs) {
    cprintf("  edi  0x%08x\n", regs->reg_edi);
    cprintf("  esi  0x%08x\n", regs->reg_esi);
    cprintf("  ebp  0x%08x\n", regs->reg_ebp);
    // cprintf("  oesp 0x%08x\n", regs->unused);
    cprintf("  ebx  0x%08x\n", regs->reg_ebx);
    cprintf("  edx  0x%08x\n", regs->reg_edx);
    cprintf("  ecx  0x%08x\n", regs->reg_ecx);
    cprintf("  eax  0x%08x\n", regs->reg_eax);
}


void print_trapframe(trap_frame *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trap_name(tf->tf_trapno));
    cprintf("  err  0x%08x\n", tf->tf_err);
    cprintf("  eip  0x%08x\n", tf->tf_eip);
}

void print_pgfault(trap_frame *tf) {
    cprintf("Page Fault at 0x%08x: %c/%c [%s].\n", rcr2(),
            (tf->tf_err & 4) ? 'U' : 'K',
            (tf->tf_err & 2) ? 'W' : 'R',
            (tf->tf_err & 1) ? "Protection Fault" : "No Page Found");
}

static void irq_timer(trap_frame *tf) {
    ticks++;
    if ((int)ticks % TICK_NUM == 0) {
        // cprintf("%d ticks\n", TICK_NUM);
    }
}

static void irq_kbd(trap_frame *tf) {
    extern void shell_handle_char(char c);
    char c = kdb_getc();
    if (c > 0) {
        shell_handle_char(c);
    }
}

static int pg_fault(trap_frame *tf) {
    print_trapframe(tf);
    print_pgfault(tf);

    vmm_pg_fault(current->mm, tf->tf_err, rcr2());

    return 0;
}

void trap(trap_frame *tf) {
    switch(tf->tf_trapno) {
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
    if (tf->tf_trapno >= IRQ_OFFSET && tf->tf_trapno < IRQ_OFFSET + 16) {
        pic_send_eoi(tf->tf_trapno - IRQ_OFFSET);
    }
}