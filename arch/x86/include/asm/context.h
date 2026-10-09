#pragma once

#define CONTEXT_FP_STATE_OFFSET 64

#ifndef __ASSEMBLY__

#include <base/types.h>
#include <asm/fpu.h>

struct Context {
    uint64_t rip{};
    uint64_t rsp{};
    uint64_t rbx{};
    uint64_t rbp{};
    uint64_t r12{};
    uint64_t r13{};
    uint64_t r14{};
    uint64_t r15{};

    // Each new task starts with empty x87 state and zeroed XMM registers.
    FloatingPointState floating_point{};

    void set_entry(uintptr_t entry_va) { rip = entry_va; }
    void set_stack(uintptr_t stack_va) { rsp = stack_va; }
    [[nodiscard]] uintptr_t entry() const { return rip; }
    [[nodiscard]] uintptr_t stack() const { return rsp; }
};

static_assert(__builtin_offsetof(Context, floating_point) == CONTEXT_FP_STATE_OFFSET);
static_assert(sizeof(Context) == CONTEXT_FP_STATE_OFFSET + FloatingPointState::SIZE);
static_assert(alignof(Context) == 16);

// Assembly functions for context switching (defined in switch.S)
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
