#pragma once

#include <base/types.h>

inline constexpr uint32_t ELF_MAGIC = 0x464C457FU;

struct ElfHdr64 {
    uint32_t e_magic;  // must equal ELF_MAGIC
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
};

struct ProgHdr64 {
    uint32_t p_type;    // loadable code or data, dynamic linking info,etc.
    uint32_t p_flags;   // read/write/execute bits
    uint64_t p_offset;  // file offset of segment
    uint64_t p_va;      // virtual address to map segment
    uint64_t p_pa;      // physical address
    uint64_t p_filesz;  // size of segment in file
    uint64_t p_memsz;   // size of segment in memory
    uint64_t p_align;   // required alignment
};

using ElfHdr = ElfHdr64;
using ProgHdr = ProgHdr64;

inline constexpr uint32_t ELF_PT_LOAD = 1;

inline constexpr uint32_t ELF_PF_X = 1;
inline constexpr uint32_t ELF_PF_W = 2;
inline constexpr uint32_t ELF_PF_R = 4;

inline constexpr uint16_t EM_AARCH64 = 0xB7;
inline constexpr uint16_t EM_RISCV = 0xF3;
inline constexpr uint16_t EM_X86_64 = 0x3E;

#if defined(__x86_64__) || defined(__i386__)
inline constexpr uint16_t EM_CURRENT = EM_X86_64;
#elif defined(__aarch64__)
inline constexpr uint16_t EM_CURRENT = EM_AARCH64;
#elif defined(__riscv)
inline constexpr uint16_t EM_CURRENT = EM_RISCV;
#endif