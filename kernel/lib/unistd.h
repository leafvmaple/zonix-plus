#pragma once

#include <asm/trap_numbers.h>

inline constexpr int NR_EXIT  = 1;
inline constexpr int NR_READ  = 3;
inline constexpr int NR_WRITE = 4;
inline constexpr int NR_OPEN  = 5;
inline constexpr int NR_CLOSE = 6;
inline constexpr int NR_PAUSE = 29;

// Syscall wrappers using C++ inline functions
// nr: syscall number, returns the result or -1 on error
template<typename T>
inline T syscall0(long nr) {
    long res{};
#if defined(__x86_64__)
    __asm__ volatile("int %1" : "=a"(res) : "i"(T_SYSCALL), "0"(nr));
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    res = x0;
#endif
    if (res >= 0) {
        return static_cast<T>(res);
    }
    return static_cast<T>(-1);
}
