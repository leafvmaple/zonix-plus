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
#include "sched/sched.h"
#include "mm/vmm.h"

// ARM Generic Timer: CNTV (virtual timer) frequency and state
namespace {

volatile int64_t timer_ticks = 0;

void timer_set_next() {
    // Set the virtual timer to fire after ~10ms (approximate)
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t interval = freq / 100;  // 100 Hz = 10ms
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(interval));
    // Enable the virtual timer
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));
}

}  // namespace

// Provide timer ticks for scheduler
namespace timer {
volatile int64_t ticks = 0;
}  // namespace timer

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
    timer_ticks++;
    sched::tick();
    timer_set_next();
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
 * We distinguish sync vs IRQ by checking the vector offset encoded
 * in the SPSR's exception class.
 */
extern "C" void trap(TrapFrame* tf) {
    uint32_t ec = (tf->esr >> 26) & 0x3F;  // Exception Class

    // Check if this is an IRQ (ESR is 0 or irrelevant for IRQs)
    // IRQs arrive via vectors at offset 0x080/0x280/0x480 (IRQ vectors).
    // For synchronous exceptions, EC != 0. For IRQs, EC is typically 0
    // because ESR is not updated on IRQ entry.
    // We use a simple heuristic: if EC == 0 and this looks like a timer,
    // handle as IRQ. The vector table could also pass type info.
    //
    // Better approach: check if the virtual timer fired.
    uint64_t cntv_ctl;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(cntv_ctl));
    bool timer_pending = (cntv_ctl & 0x5) == 0x5;  // ENABLE=1, ISTATUS=1

    if (ec == 0 && timer_pending) {
        // This is a timer IRQ
        irq_timer(tf);
    } else {
        switch (ec) {
            case T_SVC64: syscall(tf); break;
            case T_DABT_EL0:
            case T_DABT_EL1:
            case T_IABT_EL0:
            case T_IABT_EL1: pg_fault(tf); break;
            default:
                if (ec == 0 && !timer_pending) {
                    // Unknown IRQ or spurious — ignore for now
                } else {
                    cprintf("Unhandled exception: EC=0x%x ESR=0x%lx ELR=0x%lx\n", ec, tf->esr, tf->pc);
                    tf->print();
                    arch_halt_forever();
                }
                break;
        }
    }

    // Check reschedule
    TaskStruct* cur = sched::current();
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}
