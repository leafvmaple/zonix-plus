#pragma once

/* IRQ constants shared with assembly — MUST remain as macros */
#define IRQ_OFFSET 0x20  /* PIC remaps IRQ 0 to vector 0x20 */
#define IRQ_TIMER  0
#define IRQ_KBD    1
#define IRQ_SLAVE  2     /* Cascade (PIC2 connected here) */
#define IRQ_RTC    8
#define IRQ_IDE1   14
#define IRQ_IDE2   15
#define IRQ_COUNT  16

#ifndef __ASSEMBLY__

#include <base/types.h>

inline constexpr int T_DIVIDE  = 0;   // Divide error
inline constexpr int T_DEBUG   = 1;   // Debug
inline constexpr int T_NMI     = 2;   // Non-Maskable Interrupt
inline constexpr int T_BRKPT   = 3;   // Breakpoint
inline constexpr int T_OFLOW   = 4;   // Overflow
inline constexpr int T_BOUND   = 5;   // BOUND Range Exceeded
inline constexpr int T_ILLOP   = 6;   // Invalid Opcode
inline constexpr int T_DEVICE  = 7;   // Device Not Available
inline constexpr int T_DBLFLT  = 8;   // Double Fault
inline constexpr int T_TSS     = 10;  // Invalid TSS
inline constexpr int T_SEGNP   = 11;  // Segment Not Present
inline constexpr int T_STACK   = 12;  // Stack Fault
inline constexpr int T_GPFLT   = 13;  // General Protection
inline constexpr int T_PGFLT   = 14;  // Page Fault
inline constexpr int T_FPERR   = 16;  // x87 FPU Floating-Point Error
inline constexpr int T_ALIGN   = 17;  // Alignment Check
inline constexpr int T_MCHK    = 18;  // Machine-Check
inline constexpr int T_SIMDERR = 19;  // SIMD Floating-Point Exception

inline constexpr int T_SYSCALL = 0x80;

inline constexpr int TRAP_VECTOR_PGFAULT = T_PGFLT;
inline constexpr int TRAP_VECTOR_SYSCALL = T_SYSCALL;

inline constexpr int TRAP_VECTOR_IRQ_TIMER = IRQ_OFFSET + IRQ_TIMER;
inline constexpr int TRAP_VECTOR_IRQ_KBD   = IRQ_OFFSET + IRQ_KBD;
inline constexpr int TRAP_VECTOR_IRQ_IDE1  = IRQ_OFFSET + IRQ_IDE1;
inline constexpr int TRAP_VECTOR_IRQ_IDE2  = IRQ_OFFSET + IRQ_IDE2;

#endif /* !__ASSEMBLY__ */
