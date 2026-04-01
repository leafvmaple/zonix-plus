/**
 * @file trap.cpp
 * @brief RISC-V (rv64) trap/exception dispatcher.
 *
 * Routes traps based on scause:
 *   - Interrupts  (bit 63 = 1): timer, external (PLIC)
 *   - Exceptions  (bit 63 = 0): ecall (syscall), page faults, etc.
 */

#include "trap/trap.h"
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/trap_numbers.h>
#include <asm/trapframe.h>

#include "drivers/plic.h"
#include "drivers/timer.h"
#include "drivers/uart16550.h"

void TrapFrame::print() const {
    cprintf("TrapFrame at %p\n", this);
    for (int i = 0; i < 32; i++) {
        cprintf("  x%-2d  0x%016lx\n", i, regs[i]);
    }
    cprintf("  sepc     0x%016lx\n", sepc);
    cprintf("  sstatus  0x%016lx\n", sstatus);
    cprintf("  scause   0x%016lx\n", scause);
    cprintf("  stval    0x%016lx\n", stval);
}

void TrapFrame::print_pgfault() const {
    bool is_write = (scause == CAUSE_STORE_PAGE_FAULT);
    bool is_user = (sstatus & (1 << 8)) == 0; /* SPP = 0 → was U-mode */

    cprintf("Page Fault at 0x%016lx: %c/%c [%s]\n", stval, is_user ? 'U' : 'K', is_write ? 'W' : 'R',
            scause == CAUSE_FETCH_PAGE_FAULT  ? "Fetch"
            : scause == CAUSE_LOAD_PAGE_FAULT ? "Load"
                                              : "Store");
}

namespace trap {

bool arch_try_handle_irq(TrapFrame* tf) {
    if (!tf) {
        return false;
    }

    uint64_t cause = tf->scause;

    /* Check interrupt bit (bit 63) */
    if (!(cause & SCAUSE_INTR_BIT)) {
        return false; /* not an interrupt */
    }

    uint64_t code = cause & ~SCAUSE_INTR_BIT;

    if (cause == IRQ_SUPERVISOR_TIMER) {
        trap::handle_timer_tick();
        timer::set_next();
        return true;
    }

    if (cause == IRQ_SUPERVISOR_EXT) {
        /* Claim the IRQ from the PLIC */
        uint32_t irq = plic::claim();
        if (irq == static_cast<uint32_t>(IRQ_UART)) {
            uart16550::intr();
        }
        if (irq != 0) {
            plic::complete(irq);
        }
        return true;
    }

    /* Software interrupt — ignore for now */
    (void)code;
    return true;
}

bool arch_is_page_fault(const TrapFrame* tf) {
    if (!tf) {
        return false;
    }
    switch (static_cast<int>(tf->scause)) {
        case CAUSE_FETCH_PAGE_FAULT:
        case CAUSE_LOAD_PAGE_FAULT:
        case CAUSE_STORE_PAGE_FAULT: return true;
        default: return false;
    }
}

uint32_t arch_page_fault_error(const TrapFrame* tf) {
    if (!tf) {
        return 0;
    }
    uint32_t err = 0;
    if (tf->scause == CAUSE_STORE_PAGE_FAULT) {
        err |= 2; /* write fault */
    }
    if ((tf->sstatus & (1 << 8)) == 0) {
        err |= 4; /* user mode (SPP = 0) */
    }
    return err;
}

uintptr_t arch_page_fault_addr(const TrapFrame* tf) {
    return tf ? tf->stval : 0;
}

bool arch_is_syscall(const TrapFrame* tf) {
    return tf && (tf->scause == CAUSE_USER_ECALL);
}

void arch_on_syscall_entry(TrapFrame* tf) {
    if (!tf) {
        return;
    }
    /* Advance sepc past the ecall instruction (4 bytes) */
    tf->sepc += 4;
}

void arch_on_unhandled(TrapFrame* tf) {
    if (!tf) {
        return;
    }
    cprintf("Unhandled trap: scause=0x%016lx sepc=0x%016lx stval=0x%016lx\n", tf->scause, tf->sepc, tf->stval);
    tf->print();
    arch_halt_forever();
}

void arch_post_dispatch(TrapFrame* tf) {
    (void)tf;
}

}  // namespace trap
