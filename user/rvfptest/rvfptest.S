#include <abi/syscall.h>

// Each peer fills f0-f31 and fcsr with its own values and checks them across
// timer preemption and syscalls for about a second. The two peers use
// different values, so state that leaks between tasks fails.
#ifndef FP_PATTERN
#define FP_PATTERN 1237
// Round down, with the invalid-operation and inexact flags.
#define FP_FCSR 0x51
#endif
#define FP_RUN_MS 1000

.text
.globl _start
_start:
    // An exec starts with clean state, even when another task has live FP.
    frcsr t0
    bnez  t0, .Lfailure
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmv.x.d t0, f\n
    bnez  t0, .Lfailure
    .endr

    call  check_arithmetic
    beqz  a0, .Lfailure

    li    t0, FP_FCSR
    fscsr t0
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    li    t0, (FP_PATTERN + \n)
    fmv.d.x f\n, t0
    .endr

    li    a7, NR_UPTIME_MS
    ecall
    addi  s0, a0, FP_RUN_MS
.Lloop:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmv.x.d t0, f\n
    li    t1, (FP_PATTERN + \n)
    bne   t0, t1, .Lfailure
    .endr
    frcsr t0
    li    t1, FP_FCSR
    bne   t0, t1, .Lfailure
    // A zero-byte write traverses the real syscall path without console noise.
    li    a0, 1
    mv    a1, sp
    li    a2, 0
    li    a7, NR_WRITE
    ecall
    bnez  a0, .Lfailure
    li    a7, NR_UPTIME_MS
    ecall
    bltu  a0, s0, .Lloop
    li    a0, 0
    j     .Lexit
.Lfailure:
    li    a0, 1
.Lexit:
    li    a7, NR_EXIT
    ecall
    ebreak

.section .note.GNU-stack,"",@progbits
