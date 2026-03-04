#include "idt.h"

#include <base/types.h>
#include <asm/segments.h>

#include "lib/unistd.h"

extern GateDesc __idt[];
extern "C" uintptr_t __vectors[];

namespace idt {

void init() {
    for (int i = 0; i < 256; i++)
        SET_TRAP_GATE(&__idt[i], __vectors[i]);
    SET_SYS_GATE(&__idt[T_SYSCALL], __vectors[T_SYSCALL]);
}

}  // namespace idt