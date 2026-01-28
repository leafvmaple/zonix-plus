/* MBR linker script - used to generate MBR boot sector */
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS {
    /* MBR relocated to 0x0600 */
    . = 0x0600;

    .text : {
        *mbr.o(.text)
    }

    .data : {
        *mbr.o(.data)
        *mbr.o(.rodata)
    }

    /DISCARD/ : {
        *(.eh_*)
        *(.comment)
    }

    /* Ensure total size is exactly 512 bytes */
    . = 0x0600 + 512;
}
