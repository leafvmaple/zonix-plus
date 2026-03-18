#pragma once

#include <base/types.h>
#include <asm/trapframe.h>

namespace trap {

void handle_timer_tick();
int handle_page_fault(TrapFrame* tf, uint32_t err, uintptr_t fault_addr);
bool handle_syscall(TrapFrame* tf);

// Architecture hook points implemented in arch/<arch>/kernel/trap.cpp.
bool arch_try_handle_irq(TrapFrame* tf);
bool arch_is_page_fault(const TrapFrame* tf);
uint32_t arch_page_fault_error(const TrapFrame* tf);
uintptr_t arch_page_fault_addr(const TrapFrame* tf);
bool arch_is_syscall(const TrapFrame* tf);
void arch_on_syscall_entry(TrapFrame* tf);
void arch_on_unhandled(TrapFrame* tf);
void arch_post_dispatch(TrapFrame* tf);

}  // namespace trap
