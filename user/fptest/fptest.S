#include <abi/syscall.h>

// Each peer fills every vector register, FPCR and FPSR with its own values and
// checks them across timer preemption and syscalls for about a second. The
// two peers use different values, so state that leaks between tasks fails.
#ifndef FP_PATTERN
// Patterns stay below 4064 so pattern + 31 fits a compare immediate.
#define FP_PATTERN 1237
// Flush-to-zero and round towards plus infinity.
#define FP_FPCR 0x01400000
// The saturation and invalid-operation flags.
#define FP_FPSR 0x08000001
#endif
#define FP_RUN_MS 1000

.text
.globl _start
_start:
    // An exec starts with clean state, even when another task has live SIMD.
    mrs  x0, fpcr
    cbnz x0, .Lfailure
    mrs  x0, fpsr
    cbnz x0, .Lfailure
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmov x0, d\n
    cbnz x0, .Lfailure
    umov x0, v\n\().d[1]
    cbnz x0, .Lfailure
    .endr

    bl   check_arithmetic
    cbz  w0, .Lfailure

    mov  x0, #(FP_FPCR & 0xFFFF)
    movk x0, #(FP_FPCR >> 16), lsl #16
    msr  fpcr, x0
    mov  x0, #(FP_FPSR & 0xFFFF)
    movk x0, #(FP_FPSR >> 16), lsl #16
    msr  fpsr, x0
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    mov  x0, #(FP_PATTERN + \n)
    fmov d\n, x0
    ins  v\n\().d[1], x0
    .endr

    mov  x8, #NR_UPTIME_MS
    svc  #0
    add  x19, x0, #FP_RUN_MS
.Lloop:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmov x0, d\n
    cmp  x0, #(FP_PATTERN + \n)
    b.ne .Lfailure
    umov x0, v\n\().d[1]
    cmp  x0, #(FP_PATTERN + \n)
    b.ne .Lfailure
    .endr
    mrs  x0, fpcr
    mov  x1, #(FP_FPCR & 0xFFFF)
    movk x1, #(FP_FPCR >> 16), lsl #16
    cmp  x0, x1
    b.ne .Lfailure
    mrs  x0, fpsr
    mov  x1, #(FP_FPSR & 0xFFFF)
    movk x1, #(FP_FPSR >> 16), lsl #16
    cmp  x0, x1
    b.ne .Lfailure
    // A zero-byte write traverses the real syscall path without console noise.
    mov  x0, #1
    mov  x1, sp
    mov  x2, #0
    mov  x8, #NR_WRITE
    svc  #0
    cbnz x0, .Lfailure
    mov  x8, #NR_UPTIME_MS
    svc  #0
    cmp  x0, x19
    b.lo .Lloop
    mov  x0, #0
    b    .Lexit
.Lfailure:
    mov  x0, #1
.Lexit:
    mov  x8, #NR_EXIT
    svc  #0
    brk  #0

.section .note.GNU-stack,"",@progbits
