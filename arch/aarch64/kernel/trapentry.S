#include <asm/trapframe.h>
#include <asm/overflow_stack.h>

/**
 * @file trapentry.S
 * @brief AArch64 exception vector table and trap entry/exit.
 *
 * AArch64 requires a 2KB-aligned exception vector table installed
 * via VBAR_EL1.  Each vector entry is exactly 128 bytes (32 insns).
 *
 * The table has 4 groups × 4 vectors:
 *   Group 0: Current EL with SP_EL0  (not used in our kernel)
 *   Group 1: Current EL with SP_ELx  (kernel exceptions)
 *   Group 2: Lower EL, AArch64       (user-mode traps)
 *   Group 3: Lower EL, AArch32       (not used)
 *
 * Each group has: Synchronous, IRQ, FIQ, SError
 *
 * TrapFrame layout (must match struct TrapFrame in trapframe.h):
 *   0x000..0x0F0:  x0-x30     (31 regs × 8 bytes = 248)
 *   0x0F8:         sp         (SP_EL0)
 *   0x100:         pc         (ELR_EL1)
 *   0x108:         pstate     (SPSR_EL1)
 *   0x110:         esr        (ESR_EL1)
 *   0x118:         far        (FAR_EL1)
 *   0x120:         vector     (vector slot cause)
 *   Total: 0x130 = 304 bytes, including alignment padding
 */

/* ================================================================
 * Macro: save all registers into TrapFrame on the kernel stack
 * ================================================================ */
.macro SAVE_ALL vector
    /* Allocate TrapFrame on the stack */
    sub  sp, sp, #ARM64_TRAP_FRAME_SIZE

    /* Save x0-x30 */
    stp  x0,  x1,  [sp, #0x000]
    stp  x2,  x3,  [sp, #0x010]
    stp  x4,  x5,  [sp, #0x020]
    stp  x6,  x7,  [sp, #0x030]
    stp  x8,  x9,  [sp, #0x040]
    stp  x10, x11, [sp, #0x050]
    stp  x12, x13, [sp, #0x060]
    stp  x14, x15, [sp, #0x070]
    stp  x16, x17, [sp, #0x080]
    stp  x18, x19, [sp, #0x090]
    stp  x20, x21, [sp, #0x0A0]
    stp  x22, x23, [sp, #0x0B0]
    stp  x24, x25, [sp, #0x0C0]
    stp  x26, x27, [sp, #0x0D0]
    stp  x28, x29, [sp, #0x0E0]
    str  x30,      [sp, #0x0F0]

    /* Save SP_EL0 (user stack pointer) */
    mrs  x10, sp_el0
    str  x10, [sp, #0x0F8]

    /* Save ELR_EL1 (return address) */
    mrs  x10, elr_el1
    str  x10, [sp, #0x100]

    /* Save SPSR_EL1 (saved processor state) */
    mrs  x10, spsr_el1
    str  x10, [sp, #0x108]

    /* Save ESR_EL1 (exception syndrome) */
    mrs  x10, esr_el1
    str  x10, [sp, #0x110]

    /* Save FAR_EL1 (fault address) */
    mrs  x10, far_el1
    str  x10, [sp, #0x118]
    /* The vector slot determines the cause; IRQ/FIQ do not supply ESR. */
    mov  x10, #\vector
    str  x10, [sp, #ARM64_TRAP_VECTOR_OFFSET]
.endm

/* ================================================================
 * Macro: vector slot for a trap taken from the kernel.
 *
 * A frame that would reach below the floor of the current kernel
 * stack is built on the overflow stack instead. TPIDR_EL1 is unused
 * by the kernel and serves as the scratch register; a floor of zero
 * never matches. The save itself does not fit in the 128-byte slot
 * alongside the check, so it continues out of line.
 * ================================================================ */
.macro KERNEL_VECTOR name
    msr  tpidr_el1, x0
    adrp x0, __kernel_stack_floor
    ldr  x0, [x0, :lo12:__kernel_stack_floor]
    add  x0, x0, #ARM64_TRAP_FRAME_SIZE
    cmp  sp, x0         /* the interrupted flags are already in SPSR_EL1 */
    b.lo kernel_overflow_\name
    mrs  x0, tpidr_el1
    b    kernel_trap_\name
.endm

/* ================================================================
 * Macro: out-of-line bodies of one kernel vector
 * ================================================================ */
.macro KERNEL_TRAP name, vector
kernel_trap_\name:
    SAVE_ALL \vector
    mov  x0, sp
    bl   trap_dispatch
    b    trapret

kernel_overflow_\name:
    /* Kernel stack exhausted: report it from the overflow stack. */
    adrp x0, __overflow_stack
    add  x0, x0, :lo12:__overflow_stack
    add  x0, x0, #OVERFLOW_STACK_SIZE
    mov  sp, x0
    mrs  x0, tpidr_el1
    SAVE_ALL \vector
    mov  x0, sp
    bl   kernel_stack_overflow
    b    .
.endm

/* ================================================================
 * Exception vector table — must be 2KB aligned
 * Each entry is exactly 128 bytes (0x80).
 * ================================================================ */
.section .text
.balign 2048
.globl _vectors
_vectors:

/* ---- Group 0: Current EL with SP_EL0 (unused) ---- */
/* 0x000: Synchronous */
    SAVE_ALL ARM64_TRAP_VECTOR_SYNC
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 1 * 0x80

/* 0x080: IRQ */
    SAVE_ALL ARM64_TRAP_VECTOR_IRQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 2 * 0x80

/* 0x100: FIQ */
    SAVE_ALL ARM64_TRAP_VECTOR_FIQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 3 * 0x80

/* 0x180: SError */
    SAVE_ALL ARM64_TRAP_VECTOR_SERROR
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 4 * 0x80

/* ---- Group 1: Current EL with SP_ELx (kernel traps) ---- */
/* 0x200: Synchronous */
    KERNEL_VECTOR sync
.org _vectors + 5 * 0x80

/* 0x280: IRQ */
    KERNEL_VECTOR irq
.org _vectors + 6 * 0x80

/* 0x300: FIQ */
    KERNEL_VECTOR fiq
.org _vectors + 7 * 0x80

/* 0x380: SError */
    KERNEL_VECTOR serror
.org _vectors + 8 * 0x80

/* ---- Group 2: Lower EL, AArch64 (user-mode traps) ---- */
/* 0x400: Synchronous */
    SAVE_ALL ARM64_TRAP_VECTOR_SYNC
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 9 * 0x80

/* 0x480: IRQ */
    SAVE_ALL ARM64_TRAP_VECTOR_IRQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 10 * 0x80

/* 0x500: FIQ */
    SAVE_ALL ARM64_TRAP_VECTOR_FIQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 11 * 0x80

/* 0x580: SError */
    SAVE_ALL ARM64_TRAP_VECTOR_SERROR
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 12 * 0x80

/* ---- Group 3: Lower EL, AArch32 (unused) ---- */
/* 0x600: Synchronous */
    SAVE_ALL ARM64_TRAP_VECTOR_SYNC
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 13 * 0x80

/* 0x680: IRQ */
    SAVE_ALL ARM64_TRAP_VECTOR_IRQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 14 * 0x80

/* 0x700: FIQ */
    SAVE_ALL ARM64_TRAP_VECTOR_FIQ
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 15 * 0x80

/* 0x780: SError */
    SAVE_ALL ARM64_TRAP_VECTOR_SERROR
    mov  x0, sp
    bl   trap_dispatch
    b    trapret
.org _vectors + 16 * 0x80

.size _vectors, . - _vectors

    KERNEL_TRAP sync,   ARM64_TRAP_VECTOR_SYNC
    KERNEL_TRAP irq,    ARM64_TRAP_VECTOR_IRQ
    KERNEL_TRAP fiq,    ARM64_TRAP_VECTOR_FIQ
    KERNEL_TRAP serror, ARM64_TRAP_VECTOR_SERROR
.section .note.GNU-stack,"",@progbits
