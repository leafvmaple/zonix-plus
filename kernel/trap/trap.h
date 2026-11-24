#pragma once

#include <base/types.h>
#include <asm/trapframe.h>

namespace trap {

// Common fault attributes; each architecture translates its trap encoding.
inline constexpr uint32_t PAGE_FAULT_PROTECTION = 1U << 0;
inline constexpr uint32_t PAGE_FAULT_WRITE = 1U << 1;
inline constexpr uint32_t PAGE_FAULT_USER = 1U << 2;
inline constexpr uint32_t PAGE_FAULT_EXECUTE = 1U << 4;

void handle_timer_tick();
uint64_t uptime_ms();

// Counts timer interrupts since boot.
class Clock {
public:
    // The hardware backends tick at 100 Hz.
    static constexpr unsigned int TICK_MS = 10;

    static uint64_t ticks() { return ticks_; }

private:
    friend void handle_timer_tick();

    inline static volatile uint64_t ticks_{};
};

int handle_page_fault(TrapFrame* tf, uint32_t err, uintptr_t fault_addr);
// Logs which user program is about to be killed for a fault, and where.
void report_user_crash(const TrapFrame* tf, const char* what, uintptr_t address);
// Panics with the task's identity if a kernel fault at `address` hit the guard
// page below the current task's stack. Returns for any other address.
void check_stack_overflow(uintptr_t address);
// Panics with the current task's identity. Architectures whose trap entry
// detects an exhausted kernel stack call this from their overflow stack.
[[noreturn]] void stack_overflow();
bool handle_syscall(TrapFrame* tf);

// Architecture hook points implemented in arch/<arch>/kernel/trap.cpp.
bool arch_try_handle_irq(TrapFrame* tf);
bool arch_is_page_fault(const TrapFrame* tf);
uint32_t arch_page_fault_error(const TrapFrame* tf);
uintptr_t arch_page_fault_address(const TrapFrame* tf);
bool arch_is_syscall(const TrapFrame* tf);
void arch_on_syscall_entry(TrapFrame* tf);
void arch_on_unhandled(TrapFrame* tf);
void arch_post_dispatch(TrapFrame* tf);

}  // namespace trap
