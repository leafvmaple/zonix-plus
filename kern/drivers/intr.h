#pragma once

#include <arch/x86/io.h>
#include <arch/x86/cpu.h>

void intr_enable();
void intr_disable();

namespace detail {

inline int intr_save_impl() {
    if (read_eflags() & FL_IF) {
        intr_disable();
        return 1;
    }
    return 0;
}

inline void intr_restore_impl(int flag) {
    if (flag) {
        intr_enable();
    }
}

} // namespace detail

// RAII class for scoped interrupt disable
class ScopedInterruptDisable {
    int m_flag;
public:
    ScopedInterruptDisable() : m_flag(detail::intr_save_impl()) {}
    ~ScopedInterruptDisable() { detail::intr_restore_impl(m_flag); }
    
    ScopedInterruptDisable(const ScopedInterruptDisable&) = delete;
    ScopedInterruptDisable& operator=(const ScopedInterruptDisable&) = delete;
};

// Legacy compatibility macros
#define intr_save()   \
    uint32_t __intr_flag = detail::intr_save_impl();

#define intr_restore() \
    detail::intr_restore_impl(__intr_flag);
