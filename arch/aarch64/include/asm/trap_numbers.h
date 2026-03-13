#pragma once

/**
 * @file trap_numbers.h
 * @brief Architecture-specific trap and IRQ number definitions — AArch64 stub.
 *
 * AArch64 exceptions are synchronous (ESR_EL1 based) rather than vector-number
 * based like x86.  We define portable T_* aliases that map to ESR exception
 * classes so that generic kernel trap handling can use the same constants.
 *
 * TODO: refine when exception routing is implemented.
 */

// ============================================================================
// AArch64 exception classes (EC field of ESR_ELx, bits [31:26])
// ============================================================================

#define T_UNKNOWN  0x00  // Unknown reason
#define T_SVC64    0x15  // SVC instruction (AArch64)
#define T_IABT_EL0 0x20  // Instruction Abort from lower EL
#define T_IABT_EL1 0x21  // Instruction Abort from same EL
#define T_DABT_EL0 0x24  // Data Abort from lower EL
#define T_DABT_EL1 0x25  // Data Abort from same EL
#define T_SP_ALIGN 0x26  // SP alignment fault
#define T_BRK      0x3C  // BRK instruction (debug)

// ============================================================================
// Portable aliases used by kernel/ code
// ============================================================================

#define T_PGFLT   T_DABT_EL0 /* page fault from user mode  */
#define T_SYSCALL T_SVC64    /* system call trap           */

// ============================================================================
// IRQ definitions (GIC-based platform)
// ============================================================================

/* GIC SPI numbering (shared peripheral interrupts start at 32) */
#define IRQ_OFFSET 32

#define IRQ_TIMER 30 /* Non-secure EL1 physical timer (PPI) */
#define IRQ_UART  33 /* PL011 UART (SPI #1 on typical RPi)  */
#define IRQ_GPIO  49 /* GPIO controller                     */

#define IRQ_COUNT 256 /* GICv2 supports up to 1020 IDs       */
