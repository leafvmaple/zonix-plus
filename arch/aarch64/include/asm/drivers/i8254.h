#pragma once

/**
 * @file i8254.h
 * @brief AArch64 stub for x86 i8254 PIT (timer) definitions.
 *
 * AArch64 uses the ARM Generic Timer instead of PIT.
 * This header provides the constants referenced by shared kernel code.
 */

#include <base/types.h>

inline constexpr uint8_t PIT_TIMER0_REG = 0x40;
inline constexpr uint8_t PIT_CTRL_REG = 0x43;
inline constexpr uint8_t PIT_SEL_TIMER0 = 0x00;
inline constexpr uint8_t PIT_RATE_GEN = 0x04;
inline constexpr uint8_t PIT_16BIT = 0x30;
