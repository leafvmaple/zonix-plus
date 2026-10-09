#pragma once

#ifndef __ASSEMBLY__

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

    void set_entry(uintptr_t entry_va) { x30 = entry_va; }
    void set_stack(uintptr_t stack_va) { sp = stack_va; }
    [[nodiscard]] uintptr_t entry() const { return x30; }
    [[nodiscard]] uintptr_t stack() const { return sp; }
};

// Assembly functions for context switching (defined in switch.S)
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
