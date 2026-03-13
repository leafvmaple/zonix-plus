#pragma once

#ifndef __ASSEMBLY__

/**
 * @file context.h
 * @brief Architecture-specific process context for context switching.
 *
 * Each architecture must provide this header defining:
 *   - struct Context   — callee-saved register set (layout must match switch.S)
 *   - forkret()        — assembly stub for new-process first return
 *   - switch_to()      — assembly context switch routine
 */

#include <base/types.h>

// x86_64 callee-saved registers for context switching.
// Layout must match the push/pop sequence in switch.S.
struct Context {
    uint64_t rip{};
    uint64_t rsp{};
    uint64_t rbx{};
    uint64_t rbp{};
    uint64_t r12{};
    uint64_t r13{};
    uint64_t r14{};
    uint64_t r15{};

    // Portable accessors for generic kernel code
    void set_entry(uintptr_t addr) { rip = addr; }
    void set_stack(uintptr_t sp) { rsp = sp; }
};

// Assembly functions for context switching (defined in switch.S)
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
