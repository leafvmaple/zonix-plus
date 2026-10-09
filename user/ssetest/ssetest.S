#ifndef SSE_PATTERN
#define SSE_PATTERN 4951
#define SSE_MXCSR 0x3f80
#define SSE_CONTROL_WORD 0x077f
#endif

.text
.globl _start
_start:
    subq $64, %rsp
    // An exec starts with clean state, even when another task has live SIMD.
    stmxcsr (%rsp)
    cmpl $0x1f80, (%rsp)
    jne .Lfailure
    fnstcw (%rsp)
    cmpw $0x037f, (%rsp)
    jne .Lfailure
    fnstsw %ax
    testw %ax, %ax
    jne .Lfailure
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu %xmm\n, (%rsp)
    movq (%rsp), %rax
    orq 8(%rsp), %rax
    jne .Lfailure
    .endr

    call check_arithmetic
    testl %eax, %eax
    jz .Lfailure

    movl $SSE_MXCSR, (%rsp)
    ldmxcsr (%rsp)
    movw $SSE_CONTROL_WORD, (%rsp)
    fldcw (%rsp)
    movl $SSE_PATTERN, (%rsp)
    fildl (%rsp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movl $(SSE_PATTERN + \n), %eax
    movq %rax, %xmm\n
    punpcklqdq %xmm\n, %xmm\n
    .endr

    // QEMU exposes a monotonic TSC. Keep both peers alive for many timer ticks.
    rdtsc
    shlq $32, %rdx
    orq %rdx, %rax
    leaq 1000000000(%rax), %r12
.Lloop:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu %xmm\n, (%rsp)
    cmpq $(SSE_PATTERN + \n), (%rsp)
    jne .Lfailure
    cmpq $(SSE_PATTERN + \n), 8(%rsp)
    jne .Lfailure
    .endr
    stmxcsr (%rsp)
    cmpl $SSE_MXCSR, (%rsp)
    jne .Lfailure
    fnstcw (%rsp)
    cmpw $SSE_CONTROL_WORD, (%rsp)
    jne .Lfailure
    fstl (%rsp)
    movq .Lexpected_x87(%rip), %rax
    cmpq %rax, (%rsp)
    jne .Lfailure
    // A zero-byte write traverses the real syscall path without console noise.
    movl $4, %eax
    movl $1, %edi
    movq %rsp, %rsi
    xorl %edx, %edx
    int $0x80
    testq %rax, %rax
    jne .Lfailure
    rdtsc
    shlq $32, %rdx
    orq %rdx, %rax
    cmpq %r12, %rax
    jb .Lloop
    xorl %edi, %edi
    jmp .Lexit
.Lfailure:
    movl $1, %edi
.Lexit:
    movl $1, %eax
    int $0x80
    ud2

.section .rodata
.Lexpected_x87:
    .double SSE_PATTERN
.section .note.GNU-stack,"",@progbits
