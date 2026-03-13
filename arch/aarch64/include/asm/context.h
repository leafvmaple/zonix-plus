#pragma once

#ifndef __ASSEMBLY__

/**
 * @file context.h
 * @brief Architecture-specific process context — AArch64 stub.
 *
 * Callee-saved register set for context switching.
 * Layout must match the save/restore sequence in switch.S (AArch64).
 *
 * AArch64 callee-saved: x19-x30, SP.
 * x30 (LR) serves as the "return address" equivalent of x86 RIP.
 *
 * TODO: implement when context switch is written.
 */

#include <base/types.h>

struct Context {
    uint64_t x19{};
    uint64_t x20{};
    uint64_t x21{};
    uint64_t x22{};
    uint64_t x23{};
    uint64_t x24{};
    uint64_t x25{};
    uint64_t x26{};
    uint64_t x27{};
    uint64_t x28{};
    uint64_t x29{}; /* frame pointer (FP) */
    uint64_t x30{}; /* link register (LR) — return address */
    uint64_t sp{};

    // Portable accessors for generic kernel code
    void set_entry(uintptr_t addr) { x30 = addr; }
    void set_stack(uintptr_t s) { sp = s; }
};

// Assembly functions for context switching (defined in switch.S)
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
