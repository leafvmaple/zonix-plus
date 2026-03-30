#pragma once

#ifndef __ASSEMBLY__

#include <base/types.h>

inline constexpr int T_UNKNOWN  = 0x00;  // Unknown reason
inline constexpr int T_SVC64    = 0x15;  // SVC instruction (AArch64)
inline constexpr int T_IABT_EL0 = 0x20;  // Instruction Abort from lower EL
inline constexpr int T_IABT_EL1 = 0x21;  // Instruction Abort from same EL
inline constexpr int T_DABT_EL0 = 0x24;  // Data Abort from lower EL
inline constexpr int T_DABT_EL1 = 0x25;  // Data Abort from same EL
inline constexpr int T_SP_ALIGN = 0x26;  // SP alignment fault
inline constexpr int T_BRK      = 0x3C;  // BRK instruction (debug)

inline constexpr int T_PGFLT   = T_DABT_EL0; /* page fault from user mode  */
inline constexpr int T_SYSCALL = T_SVC64;     /* system call trap           */

inline constexpr int IRQ_OFFSET = 32;

inline constexpr int IRQ_TIMER = 30; /* Non-secure EL1 physical timer (PPI) */
inline constexpr int IRQ_UART  = 33; /* PL011 UART (SPI #1 on typical RPi)  */
inline constexpr int IRQ_GPIO  = 49; /* GPIO controller                     */

inline constexpr int IRQ_COUNT = 256; /* GICv2 supports up to 1020 IDs       */

inline constexpr int TRAP_EC_SYSCALL            = T_SVC64;
inline constexpr int TRAP_EC_PGFAULT_DATA_LOWER = T_DABT_EL0;
inline constexpr int TRAP_EC_PGFAULT_DATA_SAME  = T_DABT_EL1;
inline constexpr int TRAP_EC_PGFAULT_INST_LOWER = T_IABT_EL0;
inline constexpr int TRAP_EC_PGFAULT_INST_SAME  = T_IABT_EL1;

inline constexpr int TRAP_INTID_TIMER    = 27;
inline constexpr int TRAP_INTID_UART     = 33;
inline constexpr int TRAP_INTID_SPURIOUS = 1023;

#endif /* !__ASSEMBLY__ */
