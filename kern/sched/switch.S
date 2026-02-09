.code64
.text

.globl switch_to
switch_to:                      # switch_to(from, to) -> rdi=from, rsi=to
    # Save from's callee-saved registers
    # Context layout: rip, rsp, rbx, rbp, r12, r13, r14, r15
    movq (%rsp), %rax           # save return address (rip)
    movq %rax, 0(%rdi)          # context->rip = return addr
    movq %rsp, 8(%rdi)          # context->rsp
    movq %rbx, 16(%rdi)         # context->rbx
    movq %rbp, 24(%rdi)         # context->rbp
    movq %r12, 32(%rdi)         # context->r12
    movq %r13, 40(%rdi)         # context->r13
    movq %r14, 48(%rdi)         # context->r14
    movq %r15, 56(%rdi)         # context->r15

    # Restore to's callee-saved registers
    movq 56(%rsi), %r15         # restore r15
    movq 48(%rsi), %r14         # restore r14
    movq 40(%rsi), %r13         # restore r13
    movq 32(%rsi), %r12         # restore r12
    movq 24(%rsi), %rbp         # restore rbp
    movq 16(%rsi), %rbx         # restore rbx
    movq 8(%rsi), %rsp          # restore rsp
    pushq 0(%rsi)               # push rip (return address)

    ret                         # "return" to new context's rip

.globl forkret
forkret:
    # RSP points to trapframe
    # Fall through to trapret

.globl trapret
trapret:
    # Restore general-purpose registers from TrapFrame
    # RSP should point to TrapRegisters in TrapFrame
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    addq $16, %rsp             # Skip trapno and errcode

    iretq                      # Return from interrupt (pops rip, cs, rflags, rsp, ss)

.section .note.GNU-stack,"",@progbits

