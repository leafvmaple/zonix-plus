#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

struct Context {
    uint64_t rip{};
    uint64_t rsp{};
    uint64_t rbx{};
    uint64_t rbp{};
    uint64_t r12{};
    uint64_t r13{};
    uint64_t r14{};
    uint64_t r15{};

    void set_entry(uintptr_t addr) { rip = addr; }
    void set_stack(uintptr_t sp) { rsp = sp; }
};

// Assembly functions for context switching (defined in switch.S)
extern "C" void forkret(void);
extern "C" void switch_to(struct Context* from, struct Context* to);

#endif /* !__ASSEMBLY__ */
