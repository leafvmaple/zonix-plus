#pragma once

#define OVERFLOW_STACK_SIZE 8192

#ifndef __ASSEMBLY__

#include <base/types.h>

// The stack a kernel trap is taken on when the current kernel stack has no
// room left for a trap frame, so an overflow can be reported without writing
// below the stack. The trap entry reads both symbols by their assembly names.
class OverflowStack {
public:
    // Records the lowest usable address of the running task's kernel stack;
    // zero disables the check.
    static void set_floor(uintptr_t floor) { floor_ = floor; }

private:
    [[gnu::used]] inline static uintptr_t floor_ __asm__("__kernel_stack_floor"){};
    [[gnu::used]] alignas(16) inline static uint8_t stack_[OVERFLOW_STACK_SIZE] __asm__("__overflow_stack"){};
};

#endif /* !__ASSEMBLY__ */
