#pragma once

#include <asm/syscall.h>
#include <abi/syscall.h>

// Syscall wrappers using C++ inline functions
// nr: syscall number, returns the result or -1 on error
template<typename T>
inline T syscall0(long nr) {
    long res = arch_syscall0(nr);
    if (res >= 0) {
        return static_cast<T>(res);
    }
    return static_cast<T>(-1);
}
