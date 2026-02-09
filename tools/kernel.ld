OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_main)

KERNEL_BASE = 0xFFFFFFFF80000000;
KERNEL_PHYS = 0x100000;

SECTIONS {
	. = KERNEL_BASE + KERNEL_PHYS;

	PROVIDE(KERNEL_START = .);

	.text : AT(KERNEL_PHYS) {
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