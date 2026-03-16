#include "trap/trap.h"

#include "lib/unistd.h"
#include <base/types.h>
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/page.h>
#include <asm/trap_numbers.h>

#include "drivers/i8042.h"
#include "drivers/i8253.h"
#include "drivers/ide.h"
#include "drivers/fbcons.h"
#include "cons/cons.h"
#include "sched/sched.h"
#include "mm/vmm.h"

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

static void irq_timer(TrapFrame* tf) {
    timer::ticks++;
    sched::tick();
    fbcons::tick();
}

static void irq_kbd(TrapFrame* tf) {
    i8042::intr();
}

static void irq_ide(int channel) {
    IdeManager::interrupt_handler(channel);
}

static int pg_fault(TrapFrame* tf) {
    tf->print();
    tf->print_pgfault();

    TaskStruct* current = sched::current();
    vmm::pg_fault(current->memory, tf->err, arch_fault_addr());

    return 0;
}

static void syscall(TrapFrame* tf) {
    int nr = static_cast<int>(tf->syscall_nr());
    switch (nr) {
        case NR_EXIT:
            cprintf("[PID %d] exited with code %ld\n", sched::current()->pid, tf->syscall_arg(0));
            sched::exit(static_cast<int>(tf->syscall_arg(0)));
            break;
        case NR_WRITE: {
            // sys_write(fd, buf, count) — arg0=fd, arg1=buf, arg2=count
            // For now, fd is ignored and output goes to the console.
            const auto* buf = reinterpret_cast<const char*>(tf->syscall_arg(1));
            auto count = static_cast<size_t>(tf->syscall_arg(2));
            // Validate user pointer range (must be in user-space half)
            if (reinterpret_cast<uintptr_t>(buf) >= USER_SPACE_TOP ||
                count > USER_SPACE_TOP - reinterpret_cast<uintptr_t>(buf)) {
                tf->set_return(static_cast<uint64_t>(-1));
                break;
            }
            for (size_t i = 0; i < count; i++) {
                cons::putc(buf[i]);
            }
            tf->set_return(count);
            break;
        }
        default:
            cprintf("unknown syscall %d\n", nr);
            tf->set_return(static_cast<uint64_t>(-1));
            break;
    }
}

void trap(TrapFrame* tf) {
    switch (tf->trapno) {
        case T_PGFLT: pg_fault(tf); break;
        case IRQ_OFFSET + IRQ_TIMER: irq_timer(tf); break;
        case IRQ_OFFSET + IRQ_KBD: irq_kbd(tf); break;
        case IRQ_OFFSET + IRQ_IDE1: irq_ide(0); break;
        case IRQ_OFFSET + IRQ_IDE2: irq_ide(1); break;
        case T_SYSCALL: syscall(tf); break;
        default: break;
    }

    // Send EOI for hardware interrupts
    if (tf->trapno >= IRQ_OFFSET && tf->trapno < IRQ_OFFSET + IRQ_COUNT) {
        arch_irq_eoi(tf->trapno - IRQ_OFFSET);
    }

    // Check reschedule AFTER EOI — never context-switch with EOI pending,
    // otherwise the PIC blocks all further interrupts.
    TaskStruct* cur = sched::current();
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}