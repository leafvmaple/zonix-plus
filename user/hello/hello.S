# =============================================================================
# hello.S — Minimal user-mode "Hello, World!" for Zonix OS (x86_64)
#
# This program writes "Hello from userspace!\n" to the serial port (0x3F8)
# using OUT instructions, then enters an infinite HLT loop.
#
# Note: Until Zonix has a syscall interface we talk directly to hardware.
# Once sys_write / sys_exit exist, this should be converted to syscalls.
# =============================================================================

.section .text
.globl _start
_start:
    # Print greeting via serial port 0x3F8 (COM1)
    leaq    message(%rip), %rsi
    movq    $message_len, %rcx

.Lprint_loop:
    # Wait until Transmit Holding Register is empty (bit 5 of LSR @ 0x3FD)
.Lwait_thr:
    movw    $0x3FD, %dx
    inb     %dx, %al
    testb   $0x20, %al
    jz      .Lwait_thr

    # Send one character
    movw    $0x3F8, %dx
    lodsb                       # al = *rsi++
    outb    %al, %dx
    decq    %rcx
    jnz     .Lprint_loop

    # Exit cleanly via sys_exit(0)
    movq    $1, %rax        # NR_EXIT = 1
    xorq    %rdi, %rdi      # exit code = 0
    int     $0x80

.section .rodata
message:
    .ascii  "Hello from Zonix userspace!\n"
message_len = . - message

.section .note.GNU-stack,"",@progbits
