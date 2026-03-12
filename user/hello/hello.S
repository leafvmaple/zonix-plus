# =============================================================================
# hello.S — Minimal user-mode "Hello, World!" for Zonix OS (x86_64)
#
# This program writes "Hello from Zonix userspace!" to the console via
# the sys_write syscall (int $0x80), then exits via sys_exit.
# =============================================================================

.section .text
.globl _start
_start:
    # sys_write(fd=1, buf=message, count=message_len)
    movq    $4, %rax            # NR_WRITE = 4
    movq    $1, %rdi            # fd = 1 (stdout)
    leaq    message(%rip), %rsi # buf
    movq    $message_len, %rdx  # count
    int     $0x80

    # sys_exit(0)
    movq    $1, %rax            # NR_EXIT = 1
    xorq    %rdi, %rdi          # exit code = 0
    int     $0x80

.section .rodata
message:
    .ascii  "Hello from Zonix userspace!\n"
message_len = . - message

.section .note.GNU-stack,"",@progbits
