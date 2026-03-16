/**
 * @file trap.cpp
 * @brief AArch64 trap/exception dispatcher.
 *
 * Replaces the x86 version. Routes exceptions based on ESR_EL1
 * exception class instead of x86 trap numbers.
 */

#include "trap/trap.h"
#include "lib/stdio.h"
#include "lib/unistd.h"

#include <asm/arch.h>
#include <asm/page.h>
#include <asm/trap_numbers.h>

#include "cons/cons.h"
#include "drivers/fbcons.h"
#include "drivers/gic.h"
#include "drivers/pl011.h"
#include "drivers/timer.h"
#include "drivers/virtio_kbd.h"
#include "sched/sched.h"
#include "mm/vmm.h"

void TrapFrame::print() const {
    cprintf("trapframe at %p\n", this);
    for (int i = 0; i < 31; i++) {
        cprintf("  x%-2d  0x%016lx\n", i, regs[i]);
    }
    cprintf("  sp     0x%016lx\n", sp);
    cprintf("  pc     0x%016lx\n", pc);
    cprintf("  pstate 0x%016lx\n", pstate);
    cprintf("  esr    0x%016lx\n", esr);
    cprintf("  far    0x%016lx\n", far);
}

void TrapFrame::print_pgfault() const {
    // ISS[6] = WnR (Write not Read)
    // ISS[0] = DFSC lowest bits indicate translation/permission fault
    uint32_t iss = esr & 0x1FFFFFF;
    bool is_write = (iss >> 6) & 1;
    bool is_user = (pstate & 0xF) == 0;  // EL0t

    cprintf("Page Fault at 0x%016lx: %c/%c [%s]\n", far, is_user ? 'U' : 'K', is_write ? 'W' : 'R',
            (iss & 0x3F) <= 0x07 ? "Translation Fault" : "Permission Fault");
}

static void irq_timer(TrapFrame*) {
    timer::ticks++;
    fbcons::tick();
    sched::tick();
    timer::set_next();
}

static int pg_fault(TrapFrame* tf) {
    tf->print();
    tf->print_pgfault();

    TaskStruct* current = sched::current();
    uint32_t iss = tf->esr & 0x1FFFFFF;
    uint32_t err = 0;
    if (iss & (1 << 6))
        err |= 2;  // write fault
    if ((tf->pstate & 0xF) == 0)
        err |= 4;  // user mode
    if ((iss & 0x3F) > 0x07)
        err |= 1;  // permission fault (not translation)

    vmm::pg_fault(current->memory, err, tf->far);
    return 0;
}

static void syscall(TrapFrame* tf) {
    int nr = static_cast<int>(tf->syscall_nr());
    // Advance PC past SVC instruction (4 bytes)
    tf->pc += 4;

    switch (nr) {
        case NR_EXIT:
            cprintf("[PID %d] exited with code %ld\n", sched::current()->pid, tf->syscall_arg(0));
            sched::exit(static_cast<int>(tf->syscall_arg(0)));
            break;
        case NR_WRITE: {
            const auto* buf = reinterpret_cast<const char*>(tf->syscall_arg(1));
            auto count = static_cast<size_t>(tf->syscall_arg(2));
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

/**
 * Main trap dispatcher — called from trapentry.S vector stubs.
 *
 * AArch64 exceptions are classified by ESR_EL1 EC field (bits [31:26]).
 * IRQs don't set ESR — they arrive via the IRQ vector.
 * We distinguish sync vs IRQ by checking EC == 0.
 */

static constexpr uint32_t VTIMER_INTID = 27;
static constexpr uint32_t UART0_INTID = 33;
static constexpr uint32_t GIC_SPURIOUS = 1023;

extern "C" void trap(TrapFrame* tf) {
    uint32_t ec = (tf->esr >> 26) & 0x3F;  // Exception Class

    if (ec == 0) {
        // IRQ path: acknowledge via GIC
        uint32_t iar = gic::ack();
        uint32_t intid = iar & 0x3FF;

        if (intid == VTIMER_INTID) {
            irq_timer(tf);
            gic::send_eoi(iar);
        } else if (intid == UART0_INTID) {
            pl011::intr();
            gic::send_eoi(iar);
        } else if (intid == virtio_kbd::gic_intid()) {
            virtio_kbd::intr();
            gic::send_eoi(iar);
        } else if (intid != GIC_SPURIOUS) {
            gic::send_eoi(iar);
        }
    } else {
        switch (ec) {
            case T_SVC64: syscall(tf); break;
            case T_DABT_EL0:
            case T_DABT_EL1:
            case T_IABT_EL0:
            case T_IABT_EL1: pg_fault(tf); break;
            default:
                cprintf("Unhandled exception: EC=0x%x ESR=0x%lx ELR=0x%lx\n", ec, tf->esr, tf->pc);
                tf->print();
                arch_halt_forever();
                break;
        }
    }

    // Check reschedule
    TaskStruct* cur = sched::current();
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}
