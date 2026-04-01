/* UEFI EFI Shared-Object Linker Script for RISC-V 64-bit.
 *
 * Builds a position-independent ELF shared library at VA 0.
 * scripts/elf2efi.py then converts this to a PE32+ EFI image.
 */

OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)

ENTRY(efi_main)

SECTIONS
{
    ImageBase = .;

    .text : {
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
    }

    . = ALIGN(4096);

    .data : {
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        *(.sdata)
        *(.got.plt)
        *(.got)
    }

    . = ALIGN(4096);

    .dynamic : {
        *(.dynamic)
    }

    . = ALIGN(4096);

    .rela : {
        *(.rela.*)
    }

    . = ALIGN(4096);

    .reloc : {
        *(.reloc)
    }

    . = ALIGN(4096);

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.comment)
        *(.eh_frame)
        *(.bss)
    }
}
