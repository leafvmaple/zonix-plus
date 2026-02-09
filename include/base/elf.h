#pragma once

#include "types.h"

#define ELF_MAGIC 0x464C457F

/* ELF32 header (for 32-bit bootloader parsing) */
typedef struct elfhdr32 {
    uint32_t e_magic;
    uint8_t e_elf[12];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elfhdr32;

/* ELF64 header */
typedef struct elfhdr64 {
    uint32_t e_magic;      // must equal ELF_MAGIC
    uint8_t e_elf[12];
    uint16_t e_type;       // 1=relocatable, 2=executable, 3=shared object, 4=core image
    uint16_t e_machine;    // 0x3E=x86_64
    uint32_t e_version;    // file version, always 1
    uint64_t e_entry;      // entry point if executable
    uint64_t e_phoff;      // file position of program header or 0
    uint64_t e_shoff;      // file position of section header or 0
    uint32_t e_flags;      // architecture-specific flags, usually 0
    uint16_t e_ehsize;     // size of this elf header
    uint16_t e_phentsize;  // size of an entry in program header
    uint16_t e_phnum;      // number of entries in program header or 0
    uint16_t e_shentsize;  // size of an entry in section header
    uint16_t e_shnum;      // number of entries in section header or 0
    uint16_t e_shstrndx;   // section number that contains section name strings
} elfhdr64;

/* ELF64 program header */
typedef struct proghdr64 {
    uint32_t p_type;    // loadable code or data, dynamic linking info,etc.
    uint32_t p_flags;   // read/write/execute bits
    uint64_t p_offset;  // file offset of segment
    uint64_t p_va;      // virtual address to map segment
    uint64_t p_pa;      // physical address
    uint64_t p_filesz;  // size of segment in file
    uint64_t p_memsz;   // size of segment in memory
    uint64_t p_align;   // required alignment
} proghdr64;

/* ELF32 program header (legacy) */
typedef struct proghdr32 {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_va;
    uint32_t p_pa;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} proghdr32;

// Default aliases - use 64-bit for kernel
typedef elfhdr64 elfhdr;
typedef proghdr64 proghdr;

/* values for Proghdr::p_type */
#define ELF_PT_LOAD 1

/* flag bits for Proghdr::p_flags */
#define ELF_PF_X 1
#define ELF_PF_W 2
#define ELF_PF_R 4