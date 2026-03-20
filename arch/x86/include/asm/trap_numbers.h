#pragma once

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

#define T_SYSCALL 0x80

#define IRQ_OFFSET 0x20  // PIC remaps IRQ 0 to vector 0x20

#define IRQ_TIMER 0
#define IRQ_KBD   1
#define IRQ_SLAVE 2  // Cascade (PIC2 connected here)
#define IRQ_RTC   8
#define IRQ_IDE1  14
#define IRQ_IDE2  15

#define IRQ_COUNT 16

#define TRAP_VECTOR_PGFAULT T_PGFLT
#define TRAP_VECTOR_SYSCALL T_SYSCALL

#define TRAP_VECTOR_IRQ_TIMER (IRQ_OFFSET + IRQ_TIMER)
#define TRAP_VECTOR_IRQ_KBD   (IRQ_OFFSET + IRQ_KBD)
#define TRAP_VECTOR_IRQ_IDE1  (IRQ_OFFSET + IRQ_IDE1)
#define TRAP_VECTOR_IRQ_IDE2  (IRQ_OFFSET + IRQ_IDE2)
