#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

inline constexpr uint32_t PSTATE_N = 1 << 31; /* Negative */
inline constexpr uint32_t PSTATE_Z = 1 << 30; /* Zero */
inline constexpr uint32_t PSTATE_C = 1 << 29; /* Carry */
inline constexpr uint32_t PSTATE_V = 1 << 28; /* Overflow */
inline constexpr uint32_t PSTATE_D = 1 << 9;  /* Debug mask */
inline constexpr uint32_t PSTATE_A = 1 << 8;  /* SError mask */
inline constexpr uint32_t PSTATE_I = 1 << 7;  /* IRQ mask */
inline constexpr uint32_t PSTATE_F = 1 << 6;  /* FIQ mask */

/* Exception level encoding in SPSR_EL1 bits [3:0] */
inline constexpr uint32_t PSTATE_EL0t = 0x00; /* EL0 with SP_EL0 */
inline constexpr uint32_t PSTATE_EL1t = 0x04; /* EL1 with SP_EL0 */
inline constexpr uint32_t PSTATE_EL1h = 0x05; /* EL1 with SP_EL1 */

#endif /* !__ASSEMBLY__ */
