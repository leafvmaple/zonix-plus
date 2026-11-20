#pragma once

#define FP_STATE_CONTROL_OFFSET 512
#define FP_STATE_SIZE           528

#ifndef __ASSEMBLY__

#include <base/types.h>

// The FP/SIMD registers a task owns: the 32 128-bit vector registers, then
// FPCR and FPSR. Kernel code is built with -mgeneral-regs-only and never
// touches them, so switch_to saves and restores this state eagerly. A new
// task starts with zeroed registers, round-to-nearest and no exception flags.
struct alignas(16) FloatingPointState {
    static constexpr int VECTOR_COUNT = 32;

    uint64_t vectors[2 * VECTOR_COUNT]{};
    uint64_t fpcr{};
    uint64_t fpsr{};
};

static_assert(__builtin_offsetof(FloatingPointState, fpcr) == FP_STATE_CONTROL_OFFSET);
static_assert(__builtin_offsetof(FloatingPointState, fpsr) == FP_STATE_CONTROL_OFFSET + 8);
static_assert(sizeof(FloatingPointState) == FP_STATE_SIZE);

#endif /* !__ASSEMBLY__ */
