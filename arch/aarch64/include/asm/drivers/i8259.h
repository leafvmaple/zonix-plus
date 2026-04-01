#pragma once

/**
 * @file i8259.h
 * @brief AArch64 stub for x86 i8259 PIC definitions.
 *
 * AArch64 uses GIC (Generic Interrupt Controller) instead of PIC.
 * This header provides the constants referenced by shared kernel code
 * (e.g., blk.cpp) so it compiles without modification.
 */

#include <asm/trap_numbers.h>

/* x86 PIC IRQ lines — stubbed to harmless values for shared code */
inline constexpr int IRQ_SLAVE = 2;
inline constexpr int IRQ_KBD = 1;
inline constexpr int IRQ_IDE1 = 14;
inline constexpr int IRQ_IDE2 = 15;
