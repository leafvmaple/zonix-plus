#pragma once

/**
 * i8259.h — RISC-V stub for x86 PIC (8259A).
 * RISC-V uses PLIC (Platform-Level Interrupt Controller) instead.
 * IRQ line constants provided for shared kernel code (e.g. blk.cpp).
 */

#include <asm/trap_numbers.h>

/* x86 PIC IRQ aliases — harmless stubs for shared code */
inline constexpr int IRQ_SLAVE = 2;
inline constexpr int IRQ_KBD = 1;
inline constexpr int IRQ_IDE1 = 14;
inline constexpr int IRQ_IDE2 = 15;
