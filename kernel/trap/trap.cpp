#include "trap/trap.h"
#include "syscall/syscall.h"

#include "lib/stdio.h"

#include <asm/page.h>

#include "drivers/fbcons.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "debug/assert.h"

namespace timer {
extern volatile int64_t ticks;
}

namespace trap {

void handle_timer_tick() {
    timer::ticks = timer::ticks + 1;
    sched::tick();
    fbcons::tick();
}

int handle_page_fault(TrapFrame* tf, uint32_t err, uintptr_t fault_va) {
    if (!tf) {
        return -1;
    }

    tf->print();
    tf->print_pgfault();

    Task* current = sched::current();
    if (!current || !current->memory()) {
        return -1;
    }

    return vmm::pg_fault(current->memory(), err, fault_va);
}

bool handle_syscall(TrapFrame* tf) {
    if (!tf) {
        return false;
    }
    long result = -1;
    if (!syscall::dispatch(sched::current(), tf->syscall_nr(), tf->syscall_arg(0), tf->syscall_arg(1),
                           tf->syscall_arg(2), result)) {
        return false;
    }
    tf->set_return(static_cast<uint64_t>(result));
    return true;
}

}  // namespace trap

extern "C" void trap_dispatch(TrapFrame* tf) {
    if (!tf) {
        return;
    }

    if (trap::arch_try_handle_irq(tf)) {
        trap::arch_post_dispatch(tf);
    } else if (trap::arch_is_page_fault(tf)) {
        uint32_t err = trap::arch_page_fault_error(tf);
        uintptr_t fault_va = trap::arch_page_fault_address(tf);
        if (trap::handle_page_fault(tf, err, fault_va) != 0) {
            if (err & 4) {
                sched::exit(-1);
            }
            PANIC("unrecoverable kernel page fault at 0x%lx", fault_va);
        }
        trap::arch_post_dispatch(tf);
    } else if (trap::arch_is_syscall(tf)) {
        trap::arch_on_syscall_entry(tf);
        if (!trap::handle_syscall(tf)) {
            int nr = static_cast<int>(tf->syscall_nr());
            cprintf("unknown syscall %d\n", nr);
            tf->set_return(static_cast<uint64_t>(-1));
        }
        trap::arch_post_dispatch(tf);
    } else {
        trap::arch_on_unhandled(tf);
        if (tf->from_user()) {
            sched::exit(-1);
        }
        PANIC("unhandled kernel exception");
    }

    Task* cur = sched::current();
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}
