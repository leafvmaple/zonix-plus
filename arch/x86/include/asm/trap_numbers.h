#pragma once

/**
 * @file trap_numbers.h
 * @brief Architecture-specific exception and IRQ number definitions.
 *
 * Each architecture must provide this header defining:
 *   - CPU exception/trap vector numbers (T_*)
 *   - Software trap numbers (T_SYSCALL)
 *   - Hardware IRQ offset and line assignments (IRQ_*)
 *   - Total IRQ count (IRQ_COUNT)
 */

// ============================================================================
// x86 CPU Exception vectors (0-31)
// ============================================================================

#define T_DIVIDE  0   // Divide error
#define T_DEBUG   1   // Debug
#define T_NMI     2   // Non-Maskable Interrupt
#define T_BRKPT   3   // Breakpoint
#define T_OFLOW   4   // Overflow
#define T_BOUND   5   // BOUND Range Exceeded
#define T_ILLOP   6   // Invalid Opcode
#define T_DEVICE  7   // Device Not Available
#define T_DBLFLT  8   // Double Fault
#define T_TSS     10  // Invalid TSS
#define T_SEGNP   11  // Segment Not Present
#define T_STACK   12  // Stack Fault
#define T_GPFLT   13  // General Protection
#define T_PGFLT   14  // Page Fault
#define T_FPERR   16  // x87 FPU Floating-Point Error
#define T_ALIGN   17  // Alignment Check
#define T_MCHK    18  // Machine-Check
#define T_SIMDERR 19  // SIMD Floating-Point Exception

// ============================================================================
// Software trap
// ============================================================================

#define T_SYSCALL 0x80

// ============================================================================
// Hardware IRQ definitions (x86 PIC/APIC platform)
// ============================================================================

#define IRQ_OFFSET 0x20  // PIC remaps IRQ 0 to vector 0x20

#define IRQ_TIMER 0
#define IRQ_KBD   1
#define IRQ_SLAVE 2  // Cascade (PIC2 connected here)
#define IRQ_RTC   8
#define IRQ_IDE1  14
#define IRQ_IDE2  15

#define IRQ_COUNT 16
