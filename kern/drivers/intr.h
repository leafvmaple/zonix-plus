#pragma once

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>

void intr_enable(void);
void intr_disable(void);

static inline int __intr_save(void) {
    if (read_eflags() & FL_IF) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(int flag) {
    if (flag) {
        intr_enable();
    }
}

#define intr_save()   \
    uint32_t __intr_flag = __intr_save();

#define intr_restore() \
    __intr_restore(__intr_flag);
