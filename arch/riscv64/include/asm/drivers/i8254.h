#pragma once

/**
 * i8254.h — RISC-V stub for x86 PIT (8254 timer).
 * RISC-V uses SBI timer extension instead.
 * Constants provided for shared code compatibility.
 */

#include <base/types.h>

inline constexpr uint8_t PIT_TIMER0_REG = 0x40;
inline constexpr uint8_t PIT_CTRL_REG = 0x43;
inline constexpr uint8_t PIT_SEL_TIMER0 = 0x00;
inline constexpr uint8_t PIT_RATE_GEN = 0x04;
inline constexpr uint8_t PIT_16BIT = 0x30;
