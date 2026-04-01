OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_BASE = 0xFFFFFFC000000000;
KERNEL_PHYS = 0x80200000;    /* DRAM base 0x80000000 + OpenSBI space 0x200000 */

/*
 * All kernel sections are mapped at VMA = KERNEL_BASE + offset,
 * but physically loaded at LMA = KERNEL_PHYS + offset.
 * The UEFI bootloader or QEMU loads us to KERNEL_PHYS;
 * head.S enables Sv39 and jumps to the virtual address.
 */
PHDRS {
    all PT_LOAD FLAGS(7);   /* single RWX LOAD segment */
}

SECTIONS {
    . = KERNEL_BASE + KERNEL_PHYS;

    PROVIDE(KERNEL_START = .);

    /* Boot entry — must be the very first bytes in the binary */
    .text.boot : AT(KERNEL_PHYS) {
        *(.text.boot)
    } :all

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    /* C++ global constructors / destructors */
    .init_array : {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    }

    .fini_array : {
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);
    }

    . = ALIGN(0x1000);
    .data : {
        *(.data)
    }

    /* Boot page directory — aligned to page boundary */
    . = ALIGN(0x1000);
    .data.pgdir : {
        *(.data.pgdir)
    }

    . = ALIGN(0x1000);
    .bss (NOLOAD) : {
        PROVIDE(__bss_start = .);
        *(.bss .bss.*)
        *(COMMON)
        PROVIDE(__bss_end = .);
    }

    PROVIDE(KERNEL_END = .);

    /DISCARD/ : {
        *(.eh_frame .note.GNU-stack .comment)
    }
}
