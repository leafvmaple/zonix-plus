#pragma once

static inline long arch_syscall0(long nr) {
    register long a7 __asm__("a7") = nr;
    register long a0 __asm__("a0");
    __asm__ volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return a0;
}
