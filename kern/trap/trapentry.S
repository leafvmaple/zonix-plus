.globl _trap_entry
_trap_entry:
    pushal                                        # Push EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    pushl %esp  # *tf = esp

    call trap

    popl %esp
    popal

    addl $0x8, %esp   # ignore the number and error code
    iret

.section .note.GNU-stack,"",@progbits