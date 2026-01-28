OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_main)

SECTIONS {
	. = 0xC0100000;

	PROVIDE(KERNEL_START = .);

	.text : {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}
	
	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	. = ALIGN(0x1000);
	.data : {
		*(.data)
	}

	. = ALIGN(0x1000);
	.data.pgdir : {
		*(.data.pgdir)
	}

	.bss : {
		PROVIDE(__bss_start = .);
		*(.bss)
		PROVIDE(__bss_end = .);
	}

	PROVIDE(KERNEL_END = .);

    /DISCARD/ : {
        *(.eh_frame .note.GNU-stack)
    }
}