#pragma once

/**
 * @file i8254.h
 * @brief AArch64 stub for x86 i8254 PIT (timer) definitions.
 *
 * AArch64 uses the ARM Generic Timer instead of PIT.
 * This header provides the constants referenced by shared kernel code.
 */

#define PIT_TIMER0_REG 0x40
#define PIT_CTRL_REG   0x43
#define PIT_SEL_TIMER0 0x00
#define PIT_RATE_GEN   0x04
#define PIT_16BIT      0x30
