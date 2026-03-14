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
#ifndef IRQ_SLAVE
#define IRQ_SLAVE 2
#endif

#ifndef IRQ_KBD
#define IRQ_KBD 1
#endif

#ifndef IRQ_IDE1
#define IRQ_IDE1 14
#endif

#ifndef IRQ_IDE2
#define IRQ_IDE2 15
#endif
