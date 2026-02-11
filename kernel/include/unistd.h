#pragma once

#define T_SYSCALL 0x80

#define __NR_pause	29

// Syscall wrappers using C++ inline functions
// nr: syscall number, returns the result or -1 on error
template<typename T>
inline T syscall0(long nr) {
    long res;
    __asm__ volatile("int %1" : "=a"(res) : "i"(T_SYSCALL), "0"(nr));
    if (res >= 0)
        return static_cast<T>(res);
    return static_cast<T>(-1);
}

// Legacy macro for compatibility
#define _syscall0(type, name) \
    type name(void) {         \
        return syscall0<type>(__NR_##name); \
    }
