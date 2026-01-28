#include "idt.h"

#include <base/types.h>
#include <arch/x86/segments.h>

#include "unistd.h"

extern gate_desc __idt[];
extern uintptr_t __vectors[];

void idt_init() {
    for (int i = 0; i < 256; i++)
        SET_TRAP_GATE(&__idt[i], __vectors[i]);
    SET_SYS_GATE(&__idt[T_SYSCALL], __vectors[T_SYSCALL]);
}