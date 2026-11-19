#include "idt.h"
#include "tss.h"

#include <base/types.h>
#include <asm/segments.h>
#include <asm/trap_numbers.h>

#include "lib/unistd.h"

extern GateDesc __idt[];
extern "C" uintptr_t __vectors[];

namespace idt {

int init() {
    for (int i = 0; i < 256; i++)
        set_interrupt_gate(&__idt[i], __vectors[i]);
    set_sys_gate(&__idt[T_SYSCALL], __vectors[T_SYSCALL]);
    __idt[T_DBLFLT].gd_ist = tss::FAULT_IST;

    return 0;
}

}  // namespace idt