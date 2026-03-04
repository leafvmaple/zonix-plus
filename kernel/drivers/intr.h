#pragma once

#include <asm/arch.h>

namespace intr {

void enable();
void disable();

inline int save_impl() {
    if (arch_irq_save() & FL_IF) {
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

}  // namespace intr

// RAII class for scoped interrupt disable
class InterruptsGuard {
public:
    InterruptsGuard() : flag_(intr::save_impl()) {}
    ~InterruptsGuard() { intr::restore_impl(flag_); }

    InterruptsGuard(const InterruptsGuard&) = delete;
    InterruptsGuard& operator=(const InterruptsGuard&) = delete;

private:
    int flag_;
};
