/**
 * @file switch.S
 * @brief RISC-V context switch and trap-return routines.
 *
 * switch_to(Context* from, Context* to):
 *   Saves caller-saved registers into 'from', restores from 'to'.
 *   Layout must exactly match struct Context in asm/context.h.
 *
 * forkret:
 *   Entry point for newly forked / newly created tasks.
 *   The scheduler's switch_to() restores ra = forkret; when the new
 *   task is first scheduled, control arrives here.  SP already points
 *   to the TrapFrame set up by copy_thread() / arch_setup_kthread_tf().
 *   We fall through to trapret.
 *
 * trapret:
 *   Restores the full register set from the TrapFrame on the kernel
 *   stack and executes sret to return to the saved privilege level
 *   (S-mode for kernel threads, U-mode for user processes).
 *
 *   Before sret the routine also restores sscratch and stvec:
 *     - Returning to S-mode:  sscratch = 0, stvec = kernel_trap_vec
 *     - Returning to U-mode:  sscratch = kernel stack top,
 *                             stvec    = user_trap_vec
 *
 * Context layout (must match CTX_* offsets in context.h, each 8 bytes):
 *   0x00  ra   (x1)
 *   0x08  sp   (x2)
 *   0x10  s0   (x8)
 *   0x18  s1   (x9)
 *   0x20  s2   (x18)
 *   0x28  s3   (x19)
 *   0x30  s4   (x20)
 *   0x38  s5   (x21)
 *   0x40  s6   (x22)
 *   0x48  s7   (x23)
 *   0x50  s8   (x24)
 *   0x58  s9   (x25)
 *   0x60  s10  (x26)
 *   0x68  s11  (x27)
 *   [total 0x70 = 112 bytes = CTX_SIZE]
 *
 * TrapFrame layout is defined by TF_* offsets in trapframe.h
 * (TF_SIZE = 288 = 0x120).
 */

#include <asm/context.h>   /* CTX_* offsets   */
#include <asm/trapframe.h> /* TF_* offsets     */
#include <asm/cpu.h>       /* SSTATUS_SPP      */

.section .text

/* -------------------------------------------------------------------
 * void switch_to(Context* from, Context* to)
 *   a0 = pointer to 'from' Context struct
 *   a1 = pointer to 'to'   Context struct
 * ------------------------------------------------------------------- */
.globl switch_to
.type  switch_to, @function
switch_to:
    /* Save callee-saved registers to *from */
    sd  ra,  CTX_RA (a0)
    sd  sp,  CTX_SP (a0)
    sd  s0,  CTX_S0 (a0)
    sd  s1,  CTX_S1 (a0)
    sd  s2,  CTX_S2 (a0)
    sd  s3,  CTX_S3 (a0)
    sd  s4,  CTX_S4 (a0)
    sd  s5,  CTX_S5 (a0)
    sd  s6,  CTX_S6 (a0)
    sd  s7,  CTX_S7 (a0)
    sd  s8,  CTX_S8 (a0)
    sd  s9,  CTX_S9 (a0)
    sd  s10, CTX_S10(a0)
    sd  s11, CTX_S11(a0)

    /* Restore callee-saved registers from *to */
    ld  ra,  CTX_RA (a1)
    ld  sp,  CTX_SP (a1)
    ld  s0,  CTX_S0 (a1)
    ld  s1,  CTX_S1 (a1)
    ld  s2,  CTX_S2 (a1)
    ld  s3,  CTX_S3 (a1)
    ld  s4,  CTX_S4 (a1)
    ld  s5,  CTX_S5 (a1)
    ld  s6,  CTX_S6 (a1)
    ld  s7,  CTX_S7 (a1)
    ld  s8,  CTX_S8 (a1)
    ld  s9,  CTX_S9 (a1)
    ld  s10, CTX_S10(a1)
    ld  s11, CTX_S11(a1)

    ret   /* return to the new ra */

/* -------------------------------------------------------------------
 * forkret — first entry of a newly created task.
 * SP points to the TrapFrame; fall through to trapret.
 * ------------------------------------------------------------------- */
.globl forkret
.type  forkret, @function
forkret:
    j  trapret

/* -------------------------------------------------------------------
 * trapret — restore TrapFrame and sret.
 *
 * On entry: sp points to a struct TrapFrame on the kernel stack.
 * This routine pops the frame and returns to the privilege level
 * encoded in TF_SSTATUS.SPP.
 * ------------------------------------------------------------------- */
.globl trapret
.type  trapret, @function
trapret:
    /* Restore sepc and sstatus CSRs first (while t0 is still free) */
    ld   t0, TF_SEPC(sp)
    csrw sepc, t0
    ld   t0, TF_SSTATUS(sp)
    csrw sstatus, t0       /* also encodes SPP for sret */

    /* Decide: returning to S-mode (kernel) or U-mode (user)? */
    andi t0, t0, (1 << 8)  /* SSTATUS_SPP: 0=user, 1=supervisor */
    bnez t0, .Lto_kernel

.Lto_user:
    /* Set sscratch = kernel stack top (for next user-mode trap) */
    addi t0, sp, TF_SIZE
    csrw sscratch, t0
    /* Switch stvec to the user-mode trap vector */
    la   t0, user_trap_vec
    csrw stvec, t0
    j    .Lrestore_regs

.Lto_kernel:
    /* Sentinel: in S-mode sscratch = 0 */
    csrw sscratch, zero
    la   t0, kernel_trap_vec
    csrw stvec, t0

.Lrestore_regs:
    /* Restore x1, x3-x31 (all except x0=zero and x2=sp) */
    ld  x1,  TF_RA (sp)
    ld  x3,  TF_GP (sp)
    ld  x4,  TF_TP (sp)
    ld  x5,  TF_T0 (sp)
    ld  x6,  TF_T1 (sp)
    ld  x7,  TF_T2 (sp)
    ld  x8,  TF_S0 (sp)
    ld  x9,  TF_S1 (sp)
    ld  x10, TF_A0 (sp)
    ld  x11, TF_A1 (sp)
    ld  x12, TF_A2 (sp)
    ld  x13, TF_A3 (sp)
    ld  x14, TF_A4 (sp)
    ld  x15, TF_A5 (sp)
    ld  x16, TF_A6 (sp)
    ld  x17, TF_A7 (sp)
    ld  x18, TF_S2 (sp)
    ld  x19, TF_S3 (sp)
    ld  x20, TF_S4 (sp)
    ld  x21, TF_S5 (sp)
    ld  x22, TF_S6 (sp)
    ld  x23, TF_S7 (sp)
    ld  x24, TF_S8 (sp)
    ld  x25, TF_S9 (sp)
    ld  x26, TF_S10(sp)
    ld  x27, TF_S11(sp)
    ld  x28, TF_T3 (sp)
    ld  x29, TF_T4 (sp)
    ld  x30, TF_T5 (sp)
    ld  x31, TF_T6 (sp)

    /* Restore sp last — this also implicitly pops the TrapFrame */
    ld  sp, TF_SP(sp)
    sret

.section .note.GNU-stack,"",@progbits
