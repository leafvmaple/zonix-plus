#pragma once

#include <base/types.h>

// FXSAVE64's hardware image includes x87/MMX, MXCSR and all 16 XMM registers.
// Kernel C/C++ remains integer-only; use these operations only at explicit
// initialization/test boundaries. Interrupt handlers must not use SIMD.
class alignas(16) FloatingPointState {
public:
    static constexpr size_t SIZE = 512;
    static constexpr uint32_t INITIAL_MXCSR = 0x1f80;
    static constexpr uint16_t INITIAL_CONTROL_WORD = 0x037f;

    constexpr FloatingPointState() {
        image_[CONTROL_WORD_OFFSET] = INITIAL_CONTROL_WORD & 0xff;
        image_[CONTROL_WORD_OFFSET + 1] = INITIAL_CONTROL_WORD >> 8;
        image_[MXCSR_OFFSET] = INITIAL_MXCSR & 0xff;
        image_[MXCSR_OFFSET + 1] = INITIAL_MXCSR >> 8;
    }

    void save() { asm volatile("fxsave64 %0" : "=m"(image_) : : "memory"); }
    void restore() const { asm volatile("fxrstor64 %0" : : "m"(image_) : "memory"); }

private:
    static constexpr size_t CONTROL_WORD_OFFSET = 0;
    static constexpr size_t MXCSR_OFFSET = 24;
    uint8_t image_[SIZE]{};
};

static_assert(sizeof(FloatingPointState) == FloatingPointState::SIZE);
static_assert(alignof(FloatingPointState) == 16);

namespace fpu {
int init();
}
