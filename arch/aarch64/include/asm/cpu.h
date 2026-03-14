#pragma once

/**
 * @file cpu.h
 * @brief AArch64 CPU-specific definitions.
 *
 * AArch64 equivalent of the x86 cpu.h (EFLAGS, control registers, etc.).
 * Provides PSTATE/DAIF bit definitions used by kernel code.
 */

/* PSTATE / SPSR_ELx bits */
#define PSTATE_N (1 << 31) /* Negative */
#define PSTATE_Z (1 << 30) /* Zero */
#define PSTATE_C (1 << 29) /* Carry */
#define PSTATE_V (1 << 28) /* Overflow */
#define PSTATE_D (1 << 9)  /* Debug mask */
#define PSTATE_A (1 << 8)  /* SError mask */
#define PSTATE_I (1 << 7)  /* IRQ mask */
#define PSTATE_F (1 << 6)  /* FIQ mask */

/* Exception level encoding in SPSR_EL1 bits [3:0] */
#define PSTATE_EL0t 0x00 /* EL0 with SP_EL0 */
#define PSTATE_EL1t 0x04 /* EL1 with SP_EL0 */
#define PSTATE_EL1h 0x05 /* EL1 with SP_EL1 */

#ifndef __ASSEMBLY__
#include <base/types.h>
#endif
