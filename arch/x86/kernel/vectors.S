# Trap entry stubs and the table the IDT is built from.
#
# Each stub leaves an error code and its vector number on the stack and jumps
# to the common entry. The CPU pushes the error code itself for vectors 8,
# 10-14 and 17; the other stubs push a zero in its place.

.altmacro

.macro vector_stub number
.globl vector\number
vector\number:
    .if (\number == 8) || ((\number >= 10) && (\number <= 14)) || (\number == 17)
    .else
    pushq $0
    .endif
    pushq $\number
    jmp _trap_entry
.endm

.macro vector_entry number
    .quad vector\number
.endm

.text
.globl _trap_entry
.set index, 0
.rept 256
    vector_stub %index
    .set index, index + 1
.endr

.data
.globl __vectors
__vectors:
.set index, 0
.rept 256
    vector_entry %index
    .set index, index + 1
.endr

.section .note.GNU-stack,"",@progbits
