/**
 * @file trap.cpp
 * @brief AArch64 trap/exception dispatcher.
 *
 * Replaces the x86 version. Routes exceptions based on ESR_EL1
 * exception class instead of x86 trap numbers.
 */

#include "trap/trap.h"
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/trap_numbers.h>

#include "drivers/gic.h"
#include "drivers/pl011.h"
#include "drivers/timer.h"
#include "drivers/virtio_kbd.h"

namespace {

uint32_t trap_ec(const TrapFrame* tf) {
    if (!tf) {
        return 0;
    }
    return (tf->esr >> 26) & 0x3F;
}

}  // namespace

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

namespace trap {

bool arch_try_handle_irq(TrapFrame* tf) {
    if (!tf || trap_ec(tf) != 0) {
        return false;
    }

    uint32_t iar = gic::ack();
    uint32_t intid = iar & 0x3FF;

    if (intid == TRAP_INTID_TIMER) {
        trap::handle_timer_tick();
        timer::set_next();
        gic::send_eoi(iar);
    } else if (intid == TRAP_INTID_UART) {
        pl011::intr();
        gic::send_eoi(iar);
    } else if (intid == virtio_kbd::gic_intid()) {
        virtio_kbd::intr();
        gic::send_eoi(iar);
    } else if (intid != TRAP_INTID_SPURIOUS) {
        gic::send_eoi(iar);
    }

    return true;
}

bool arch_is_page_fault(const TrapFrame* tf) {
    uint32_t ec = trap_ec(tf);
    switch (ec) {
        case TRAP_EC_PGFAULT_DATA_LOWER:
        case TRAP_EC_PGFAULT_DATA_SAME:
        case TRAP_EC_PGFAULT_INST_LOWER:
        case TRAP_EC_PGFAULT_INST_SAME: return true;
        default: return false;
    }
}

uint32_t arch_page_fault_error(const TrapFrame* tf) {
    if (!tf) {
        return 0;
    }

    uint32_t iss = tf->esr & 0x1FFFFFF;
    uint32_t err = 0;
    if (iss & (1 << 6))
        err |= 2;  // write fault
    if ((tf->pstate & 0xF) == 0)
        err |= 4;  // user mode
    if ((iss & 0x3F) > 0x07)
        err |= 1;  // permission fault (not translation)

    return err;
}

uintptr_t arch_page_fault_addr(const TrapFrame* tf) {
    return tf ? tf->far : 0;
}

bool arch_is_syscall(const TrapFrame* tf) {
    return trap_ec(tf) == TRAP_EC_SYSCALL;
}

void arch_on_syscall_entry(TrapFrame* tf) {
    if (!tf) {
        return;
    }

    // Advance PC past SVC instruction (4 bytes)
    tf->pc += 4;
}

void arch_on_unhandled(TrapFrame* tf) {
    if (!tf) {
        return;
    }

    uint32_t ec = trap_ec(tf);
    cprintf("Unhandled exception: EC=0x%x ESR=0x%lx ELR=0x%lx\n", ec, tf->esr, tf->pc);
    tf->print();
    arch_halt_forever();
}

void arch_post_dispatch(TrapFrame* tf) {
    (void)tf;
}

}  // namespace trap
