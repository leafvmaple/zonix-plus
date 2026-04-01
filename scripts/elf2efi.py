#!/usr/bin/env python3
"""
elf2efi.py – Convert a RISC-V 64-bit ELF shared object to a UEFI PE32+ image.

Usage:
    python3 elf2efi.py input.so output.efi

The input must be a position-independent ELF shared library linked at VA 0
with a single load segment.  The tool computes the correct PE checksum,
sets machine type 0x5064 (RISC-V 64-bit), and writes the EFI application
image suitable for firmware loading.

Based on the UEFI spec PE/COFF image format and the ELF-to-PE approach used
by gnu-efi and Tiano Core GenFw.
"""

import sys
import struct
import math

# ---------- PE/COFF constants ----------
PE_MACHINE_RISCV64 = 0x5064
PE_OPT_MAGIC_PE32P = 0x020B  # PE32+ (64-bit)
PE_SUBSYSTEM_EFI_APP = 10
IMAGE_FILE_DLL = 0x2000
IMAGE_FILE_EXECUTABLE = 0x0002
IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INIT = 0x00000040
IMAGE_SCN_CNT_UNINIT = 0x00000080
IMAGE_SCN_MEM_EXEC = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

# ELF constants
ET_DYN = 3
PT_LOAD = 1
PT_DYNAMIC = 2
SHT_RELA = 4
SHT_REL = 9
SHT_DYNSYM = 11

# RISC-V relocation types
R_RISCV_RELATIVE = 3


def align_up(value, align):
    if align <= 1:
        return value
    return (value + align - 1) & ~(align - 1)


def pe_checksum(data):
    """Compute PE image checksum per the PE spec."""
    checksum = 0
    # Process as 16-bit words
    remainder = len(data) % 2
    if remainder:
        data = data + b"\x00"
    words = struct.unpack("<" + "H" * (len(data) // 2), data)
    for w in words:
        checksum += w
        if checksum > 0xFFFF:
            checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum += len(data) - remainder
    return checksum & 0xFFFFFFFF


def read_elf64(data):
    """Parse a 64-bit little-endian ELF and return useful fields."""
    if data[:4] != b"\x7fELF":
        raise ValueError("Not an ELF file")
    ei_class = data[4]
    ei_data = data[5]
    if ei_class != 2 or ei_data != 1:
        raise ValueError("Expected ELF64 little-endian")

    (e_type,) = struct.unpack_from("<H", data, 0x10)
    (e_machine,) = struct.unpack_from("<H", data, 0x12)
    (e_entry,) = struct.unpack_from("<Q", data, 0x18)
    (e_phoff,) = struct.unpack_from("<Q", data, 0x20)
    (e_shoff,) = struct.unpack_from("<Q", data, 0x28)
    (e_phnum,) = struct.unpack_from("<H", data, 0x38)
    (e_shnum,) = struct.unpack_from("<H", data, 0x3C)
    (e_shstrndx,) = struct.unpack_from("<H", data, 0x3E)

    # Parse program headers
    phdrs = []
    for i in range(e_phnum):
        off = e_phoff + i * 0x38
        (ph_type,) = struct.unpack_from("<I", data, off)
        (ph_flags,) = struct.unpack_from("<I", data, off + 4)
        (ph_offset,) = struct.unpack_from("<Q", data, off + 8)
        (ph_vaddr,) = struct.unpack_from("<Q", data, off + 16)
        (ph_filesz,) = struct.unpack_from("<Q", data, off + 32)
        (ph_memsz,) = struct.unpack_from("<Q", data, off + 40)
        (ph_align,) = struct.unpack_from("<Q", data, off + 48)
        phdrs.append(
            {
                "type": ph_type,
                "flags": ph_flags,
                "offset": ph_offset,
                "vaddr": ph_vaddr,
                "filesz": ph_filesz,
                "memsz": ph_memsz,
                "align": ph_align,
            }
        )

    # Parse section headers for RELA relocations
    shdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * 0x40
        (sh_name,) = struct.unpack_from("<I", data, off)
        (sh_type,) = struct.unpack_from("<I", data, off + 4)
        (sh_offset,) = struct.unpack_from("<Q", data, off + 24)
        (sh_size,) = struct.unpack_from("<Q", data, off + 32)
        (sh_entsize,) = struct.unpack_from("<Q", data, off + 56)
        shdrs.append(
            {
                "name": sh_name,
                "type": sh_type,
                "offset": sh_offset,
                "size": sh_size,
                "entsize": sh_entsize,
            }
        )

    # Collect R_RISCV_RELATIVE relocations (base relocs for PE)
    rel_offsets = []
    for sh in shdrs:
        if sh["type"] == SHT_RELA and sh["entsize"] >= 24:
            for j in range(0, sh["size"], sh["entsize"]):
                (r_offset,) = struct.unpack_from("<Q", data, sh["offset"] + j)
                (r_info,) = struct.unpack_from("<Q", data, sh["offset"] + j + 8)
                r_type = r_info & 0xFFFFFFFF
                if r_type == R_RISCV_RELATIVE:
                    rel_offsets.append(r_offset)

    return {
        "entry": e_entry,
        "phdrs": phdrs,
        "shdrs": shdrs,
        "rel_offsets": sorted(rel_offsets),
    }


def build_base_reloc(rel_offsets):
    """Build PE base relocation table from list of RVA offsets."""
    if not rel_offsets:
        return b""

    blocks = []
    i = 0
    while i < len(rel_offsets):
        page = rel_offsets[i] & ~0xFFF
        entries = []
        while i < len(rel_offsets) and (rel_offsets[i] & ~0xFFF) == page:
            offset = rel_offsets[i] & 0xFFF
            # Type 10 = IMAGE_REL_BASED_DIR64
            entries.append(0xA000 | offset)
            i += 1
        # Pad to 4-byte alignment
        if len(entries) % 2:
            entries.append(0)
        block_size = 8 + len(entries) * 2
        block = struct.pack("<II", page, block_size)
        block += struct.pack("<" + "H" * len(entries), *entries)
        blocks.append(block)
    return b"".join(blocks)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.so output.efi", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        elf_data = bytearray(f.read())

    elf = read_elf64(elf_data)

    # Find all LOAD segments
    load_segs = [ph for ph in elf["phdrs"] if ph["type"] == PT_LOAD]
    if not load_segs:
        raise ValueError("No PT_LOAD segments found")

    # Determine image extent
    img_start = min(ph["vaddr"] for ph in load_segs)
    img_end = max(ph["vaddr"] + ph["memsz"] for ph in load_segs)
    img_size = align_up(img_end - img_start, 0x1000)
    # entry_rva will be adjusted after we know text_rva

    # Build raw image buffer (zero-extended to img_size)
    raw = bytearray(img_size)
    for ph in load_segs:
        src_off = ph["offset"]
        dst_off = ph["vaddr"] - img_start
        copy_len = min(ph["filesz"], ph["memsz"])
        raw[dst_off : dst_off + copy_len] = elf_data[src_off : src_off + copy_len]

    # Build base relocation section
    # Adjust ELF offsets → PE RVAs: PE_offset = text_rva + (elf_va - img_start)
    # But text_rva is not yet known here; we compute it after header sizing.
    # Base reloc offsets relative to ELF VAs, adjusted below.
    raw_rel_offsets = elf["rel_offsets"]

    # --- PE layout constants ---
    FILE_ALIGN = 0x200
    SECT_ALIGN = 0x1000

    # We'll emit two PE sections: .text (the raw image) and .reloc
    num_sections = 2

    # PE header sizes
    dos_hdr_size = 0x40
    pe_sig_size = 4
    coff_hdr_size = 20
    opt_hdr_size = 240  # PE32+ optional header
    sec_hdr_size = 40 * num_sections

    hdr_raw_size = (
        dos_hdr_size + pe_sig_size + coff_hdr_size + opt_hdr_size + sec_hdr_size
    )
    hdr_file_size = align_up(hdr_raw_size, FILE_ALIGN)

    text_rva = align_up(hdr_file_size, SECT_ALIGN)  # RVA of .text in PE image
    text_raw_size = align_up(img_size, FILE_ALIGN)
    text_virt_size = img_size
    # entry_rva in PE = text_rva + (ELF entry VA - img_start)
    entry_rva = text_rva + (elf["entry"] - img_start)

    # Adjust reloc offsets from ELF VAs to PE RVAs
    pe_rel_offsets = [text_rva + (va - img_start) for va in raw_rel_offsets]
    base_reloc_data = build_base_reloc(pe_rel_offsets)
    reloc_size = len(base_reloc_data)

    reloc_rva = text_rva + align_up(text_virt_size, SECT_ALIGN)
    reloc_raw_size = align_up(reloc_size, FILE_ALIGN)
    reloc_virt_size = reloc_size

    image_size = align_up(reloc_rva + reloc_virt_size, SECT_ALIGN)

    # --- Assemble PE binary ---
    out = bytearray(hdr_file_size + text_raw_size + reloc_raw_size)

    # DOS stub — minimal, just sets e_magic and e_lfanew
    pe_offset = dos_hdr_size
    out[0:2] = b"MZ"
    struct.pack_into("<I", out, 0x3C, pe_offset)  # e_lfanew

    # PE Signature
    struct.pack_into("<4s", out, pe_offset, b"PE\x00\x00")
    coff_off = pe_offset + 4

    # COFF header
    characteristics = (
        IMAGE_FILE_EXECUTABLE | IMAGE_FILE_LARGE_ADDRESS_AWARE | IMAGE_FILE_DLL
    )
    struct.pack_into(
        "<HHIIIHH",
        out,
        coff_off,
        PE_MACHINE_RISCV64,  # Machine
        num_sections,  # NumberOfSections
        0,  # TimeDateStamp
        0,  # PointerToSymbolTable
        0,  # NumberOfSymbols
        opt_hdr_size,  # SizeOfOptionalHeader
        characteristics,  # Characteristics
    )
    opt_off = coff_off + coff_hdr_size

    # Optional header (PE32+)
    struct.pack_into(
        "<HBBiIII",
        out,
        opt_off,
        PE_OPT_MAGIC_PE32P,  # Magic
        0,
        0,  # MajorLinkerVersion, MinorLinkerVersion
        text_virt_size,  # SizeOfCode
        reloc_virt_size,  # SizeOfInitializedData
        0,  # SizeOfUninitializedData
        entry_rva,  # AddressOfEntryPoint
    )
    struct.pack_into("<I", out, opt_off + 20, text_rva)  # BaseOfCode
    struct.pack_into("<Q", out, opt_off + 24, 0)  # ImageBase (relocated by FW)
    struct.pack_into(
        "<II", out, opt_off + 32, SECT_ALIGN, FILE_ALIGN
    )  # SectionAlignment, FileAlignment
    struct.pack_into("<HH", out, opt_off + 40, 0, 0)  # OS version
    struct.pack_into("<HH", out, opt_off + 44, 0, 0)  # Image version
    struct.pack_into("<HH", out, opt_off + 48, 0, 0)  # Subsystem version
    struct.pack_into("<I", out, opt_off + 52, 0)  # Win32VersionValue
    struct.pack_into("<I", out, opt_off + 56, image_size)  # SizeOfImage
    struct.pack_into("<I", out, opt_off + 60, hdr_file_size)  # SizeOfHeaders
    # Checksum placeholder at opt_off + 64 — fill in later
    struct.pack_into("<H", out, opt_off + 68, PE_SUBSYSTEM_EFI_APP)  # Subsystem
    struct.pack_into("<H", out, opt_off + 70, 0)  # DllCharacteristics
    struct.pack_into("<Q", out, opt_off + 72, 0x200000)  # SizeOfStackReserve
    struct.pack_into("<Q", out, opt_off + 80, 0x200000)  # SizeOfStackCommit
    struct.pack_into("<Q", out, opt_off + 88, 0x1000)  # SizeOfHeapReserve
    struct.pack_into("<Q", out, opt_off + 96, 0x1000)  # SizeOfHeapCommit
    struct.pack_into("<I", out, opt_off + 104, 0)  # LoaderFlags
    struct.pack_into("<I", out, opt_off + 108, 16)  # NumberOfRvaAndSizes

    # Data directories (16 entries of RVA+size, 8 bytes each)
    dd_off = opt_off + 112
    # [0] Export, [1] Import, [2] Resource, [3] Exception,
    # [4] Security, [5] BaseReloc, ...
    # Only BaseReloc is non-zero (index 5)
    for i in range(16):
        struct.pack_into("<II", out, dd_off + i * 8, 0, 0)
    struct.pack_into("<II", out, dd_off + 5 * 8, reloc_rva, reloc_virt_size)

    # Section headers
    sh_off = opt_off + opt_hdr_size

    def write_section(offset, name, vsize, rva, raw_size, raw_ptr, chars):
        name_bytes = name.encode("ascii")[:8].ljust(8, b"\x00")
        struct.pack_into(
            "<8sIIIIIIHHI",
            out,
            offset,
            name_bytes,
            vsize,  # VirtualSize
            rva,  # VirtualAddress
            raw_size,  # SizeOfRawData
            raw_ptr,  # PointerToRawData
            0,
            0,  # PointerToRelocations, PointerToLinenumbers
            0,
            0,  # NumberOfRelocations, NumberOfLinenumbers
            chars,  # Characteristics
        )

    text_chars = (
        IMAGE_SCN_CNT_CODE
        | IMAGE_SCN_MEM_EXEC
        | IMAGE_SCN_MEM_READ
        | IMAGE_SCN_MEM_WRITE
        | IMAGE_SCN_CNT_INIT
    )
    reloc_chars = IMAGE_SCN_CNT_INIT | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE

    write_section(
        sh_off + 0 * 40,
        ".text",
        text_virt_size,
        text_rva,
        text_raw_size,
        hdr_file_size,
        text_chars,
    )
    write_section(
        sh_off + 1 * 40,
        ".reloc",
        reloc_virt_size,
        reloc_rva,
        reloc_raw_size,
        hdr_file_size + text_raw_size,
        reloc_chars,
    )

    # Copy image data into .text section
    out[hdr_file_size : hdr_file_size + img_size] = raw

    # Copy base reloc data into .reloc section
    reloc_file_off = hdr_file_size + text_raw_size
    out[reloc_file_off : reloc_file_off + reloc_size] = base_reloc_data

    # Compute and write checksum
    chk = pe_checksum(bytes(out))
    struct.pack_into("<I", out, opt_off + 64, chk)

    with open(sys.argv[2], "wb") as f:
        f.write(out)

    print(
        f"  EFI     {sys.argv[2]}  ({len(out)} bytes, {num_sections} sections, "
        f"entry RVA 0x{entry_rva:x})"
    )


if __name__ == "__main__":
    main()
