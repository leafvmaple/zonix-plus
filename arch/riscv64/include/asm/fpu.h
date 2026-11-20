#pragma once

#define FP_STATE_FCSR_OFFSET 256
#define FP_STATE_SIZE        264

#ifndef __ASSEMBLY__

#include <base/types.h>

// The FP registers a task owns: f0-f31 as 64-bit doubles, then fcsr. Kernel
// code is built for the integer-only lp64 ABI and never touches them, so
// switch_to saves and restores this state eagerly. A new task starts with
// zeroed registers, round-to-nearest and no exception flags.
struct FloatingPointState {
    static constexpr int REGISTER_COUNT = 32;

    uint64_t registers[REGISTER_COUNT]{};
    uint64_t fcsr{};
};

static_assert(__builtin_offsetof(FloatingPointState, fcsr) == FP_STATE_FCSR_OFFSET);
static_assert(sizeof(FloatingPointState) == FP_STATE_SIZE);

#endif /* !__ASSEMBLY__ */
