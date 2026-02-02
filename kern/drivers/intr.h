#pragma once

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>

namespace intr {

void enable();
void disable();

inline int save_impl() {
    if (read_eflags() & FL_IF) {
        disable();
        return 1;
    }
    return 0;
}

inline void restore_impl(int flag) {
    if (flag) {
        enable();
    }
}

} // namespace intr

// RAII class for scoped interrupt disable
class InterruptsGuard {
    int m_flag;
public:
    InterruptsGuard() : m_flag(intr::save_impl()) {}
    ~InterruptsGuard() { intr::restore_impl(m_flag); }
    
    InterruptsGuard(const InterruptsGuard&) = delete;
    InterruptsGuard& operator=(const InterruptsGuard&) = delete;
};
