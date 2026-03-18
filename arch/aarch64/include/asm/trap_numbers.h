#pragma once

#define T_UNKNOWN  0x00  // Unknown reason
#define T_SVC64    0x15  // SVC instruction (AArch64)
#define T_IABT_EL0 0x20  // Instruction Abort from lower EL
#define T_IABT_EL1 0x21  // Instruction Abort from same EL
#define T_DABT_EL0 0x24  // Data Abort from lower EL
#define T_DABT_EL1 0x25  // Data Abort from same EL
#define T_SP_ALIGN 0x26  // SP alignment fault
#define T_BRK      0x3C  // BRK instruction (debug)

#define T_PGFLT   T_DABT_EL0 /* page fault from user mode  */
#define T_SYSCALL T_SVC64    /* system call trap           */

#define IRQ_OFFSET 32

#define IRQ_TIMER 30 /* Non-secure EL1 physical timer (PPI) */
#define IRQ_UART  33 /* PL011 UART (SPI #1 on typical RPi)  */
#define IRQ_GPIO  49 /* GPIO controller                     */

#define IRQ_COUNT 256 /* GICv2 supports up to 1020 IDs       */

#define TRAP_EC_SYSCALL            T_SVC64
#define TRAP_EC_PGFAULT_DATA_LOWER T_DABT_EL0
#define TRAP_EC_PGFAULT_DATA_SAME  T_DABT_EL1
#define TRAP_EC_PGFAULT_INST_LOWER T_IABT_EL0
#define TRAP_EC_PGFAULT_INST_SAME  T_IABT_EL1

#define TRAP_INTID_TIMER    27
#define TRAP_INTID_UART     33
#define TRAP_INTID_SPURIOUS 1023
