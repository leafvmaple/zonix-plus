#pragma once

#include "trap_numbers.h"

static inline long arch_syscall0(long nr) {
    long result;
    __asm__ volatile("int %1" : "=a"(result) : "i"(T_SYSCALL), "0"(nr) : "memory");
    return result;
}
