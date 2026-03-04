/* UEFI PE32+ Linker Script for x86_64 UEFI Applications */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

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
    
    .rel : {
        *(.rel.*)
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
    
    .dynsym : {
        *(.dynsym)
    }
    
    . = ALIGN(4096);
    
    .dynstr : {
        *(.dynstr)
    }
    
    . = ALIGN(4096);
    
    .bss : {
        *(.sbss)
        *(.scommon)
        *(.dynbss)
        *(.bss .bss.*)
        *(COMMON)
    }
    
    /DISCARD/ : {
        *(.note.*)
        *(.eh_frame)
        *(.comment)
    }
}
