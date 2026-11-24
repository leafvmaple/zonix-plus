#include "trap/trap.h"
#include "syscall/syscall.h"

#include "lib/stdio.h"

#include <asm/page.h>

#include "drivers/fbcons.h"
#include "drivers/virtio_net.h"
#include "drivers/virtio_rng.h"
#include "fs/readiness.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "debug/assert.h"
#include "debug/backtrace.h"
#include <kernel/log.h>
#include <abi/process.h>

namespace trap {

uint64_t uptime_ms() {
    return Clock::ticks() * Clock::TICK_MS;
}

void handle_timer_tick() {
    Clock::ticks_ = Clock::ticks_ + 1;
    sched::tick();
    fbcons::tick();
    virtio_net::tick();
    virtio_rng::tick();
    fd::Readiness::tick();
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

void report_user_crash(const TrapFrame* tf, const char* what, uintptr_t address) {
    Task* current = sched::current();
    klog::warn("%s[%d]: %s 0x%lx, pc 0x%lx; killed\n", current ? current->name() : "?", current ? current->pid : -1,
               what, address, tf->instruction_pointer());
}

void check_stack_overflow(uintptr_t address) {
    Task* current = sched::current();
    if (current && current->stack_guard_contains(address)) {
        stack_overflow();
    }
}

void stack_overflow() {
    Task* current = sched::current();
    PANIC("kernel stack overflow in task %d (%s)", current ? current->pid : -1, current ? current->name() : "");
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
                trap::report_user_crash(tf, "page fault at", fault_va);
                sched::exit(-1);
            }
            trap::check_stack_overflow(fault_va);
            debug::log_backtrace("faulting context: pc", tf->instruction_pointer(), tf->frame_pointer());
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
            trap::report_user_crash(tf, "fatal exception at", tf->instruction_pointer());
            sched::exit(-1);
        }
        debug::log_backtrace("faulting context: pc", tf->instruction_pointer(), tf->frame_pointer());
        PANIC("unhandled kernel exception");
    }

    Task* cur = sched::current();
    if (cur && tf->from_user() && cur->termination_requested()) {
        sched::exit(PROCESS_STATUS_KILLED);
    }
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}
