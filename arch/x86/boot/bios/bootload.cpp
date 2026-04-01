// BIOS Bootloader for Zonix
// Loaded by VBR at 0x7E00, runs in 32-bit protected mode
// This bootloader:
//   1. Reads FAT16/FAT32 filesystem from disk using ATA PIO ports
//   2. Locates and loads KERNEL.SYS (ELF file) to 0x10000
//   3. Parses ELF and loads segments to their proper locations
//   4. Sets up 64-bit long mode (PAE, PML4 page tables, EFER, paging)
//   5. Jumps to kernel entry point in 64-bit mode

#include <asm/memlayout.h>
#include <asm/cr.h>
#include <asm/io.h>

#include <base/elf.h>
#include <base/types.h>
#include <kernel/bootinfo.h>
#include <base/bpb.h>

#include "../bootlib.h"

static constexpr uint32_t SECT_SIZE = 512;
static constexpr const char KERNEL_FNAME[] = "KERNEL  SYS";

static constexpr uint16_t ATA_PORT_DATA = 0x1F0;        // Data register
static constexpr uint16_t ATA_PORT_SECTOR_CNT = 0x1F2;  // Sector count
static constexpr uint16_t ATA_PORT_LBA_LOW = 0x1F3;     // LBA low byte
static constexpr uint16_t ATA_PORT_LBA_MID = 0x1F4;     // LBA mid byte
static constexpr uint16_t ATA_PORT_LBA_HIGH = 0x1F5;    // LBA high byte
static constexpr uint16_t ATA_PORT_DRIVE = 0x1F6;       // Drive/Head register
static constexpr uint16_t ATA_PORT_STATUS = 0x1F7;      // Status register (read)
static constexpr uint16_t ATA_PORT_COMMAND = 0x1F7;     // Command register (write)

static constexpr uint8_t ATA_CMD_READ_PIO = 0x20;  // Read with retry

static constexpr uint8_t ATA_STATUS_BSY = 0x80;   // Busy
static constexpr uint8_t ATA_STATUS_DRDY = 0x40;  // Drive ready

static constexpr uint32_t KERNEL_ELF_ADDR = 0x10000;

// =============================================================================
// Memory layout for 32->64 bit transition structures (placed in low memory)
//
//   0x1000 - 0x1FFF  : PML4         (4KB, 4KB-aligned)
//   0x2000 - 0x2FFF  : PDPT         (4KB, 4KB-aligned)
//   0x3000 - 0x3FFF  : PD0          (4KB, maps 0-1GB with 2MB pages)
//   0x4000 - 0x4FFF  : PD1          (4KB, maps 1-2GB with 2MB pages)
//   0x5000 - 0x5FFF  : Reserved
//   0x6000 - 0x603F  : 64-bit GDT   (3 entries)
//   0x6040 - 0x604F  : GDT descriptor
//   0x6050 - 0x60FF  : Boot info copy area
// =============================================================================
static constexpr uint32_t BOOT_PML4_ADDR = 0x1000;
static constexpr uint32_t BOOT_PDPT_ADDR = 0x2000;
static constexpr uint32_t BOOT_PD0_ADDR = 0x3000;
static constexpr uint32_t BOOT_PD1_ADDR = 0x4000;
static constexpr uint32_t BOOT_GDT64_ADDR = 0x6000;
static constexpr uint32_t BOOT_GDTDESC64_ADDR = 0x6040;

static constexpr uint32_t PT_P = 0x001;   // Present
static constexpr uint32_t PT_W = 0x002;   // Writable
static constexpr uint32_t PT_PS = 0x080;  // Page Size (2MB)

static uint8_t boot_drive{};
static auto* bpb32 = reinterpret_cast<Fat32Bpb*>(0x7C0B);  // BPB starts at 0x7C00 + 0x0B (after jmp + oem)
static BootInfo bi{};

static void ata_wait_ready() {
    while ((inb(ATA_PORT_STATUS) & (ATA_STATUS_BSY | ATA_STATUS_DRDY)) != ATA_STATUS_DRDY)
        ;
}

static int ata_read_sector(uint32_t lba, void* buffer) {
    ata_wait_ready();

    outb(ATA_PORT_SECTOR_CNT, 1);
    outb(ATA_PORT_LBA_LOW, lba & 0xFF);
    outb(ATA_PORT_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_PORT_DRIVE, ((lba >> 24) & 0xF) | 0xE0);
    outb(ATA_PORT_COMMAND, ATA_CMD_READ_PIO);

    ata_wait_ready();

    insl(ATA_PORT_DATA, buffer, SECT_SIZE >> 2);  // Read 512 bytes (256 words)

    return 0;
}

static int read_sectors(uint32_t lba, uint32_t count, uint8_t* buffer) {
    for (uint32_t i = 0; i < count; i++) {
        // Calculate absolute LBA: hidden_sectors + 1 (VBR) + lba
        uint32_t abs_lba = bpb32->common.hidden_sectors + 1 + lba + i;
        if (ata_read_sector(abs_lba, buffer + i * SECT_SIZE) != 0)
            return -1;
    }
    return 0;
}

static int load_elf_kernel(uint8_t* elf_buffer, BootInfo* bi) {
    auto* elf = reinterpret_cast<ElfHdr64*>(elf_buffer);

    if (elf->e_magic != ELF_MAGIC) {
        return -1;
    }

    bi->kernel_start = 0xFFFFFFFF;
    bi->kernel_end = 0;
    bi->kernel_entry = static_cast<uint32_t>(elf->e_entry & 0xFFFFFFFF);  // Physical entry

    auto* ph = reinterpret_cast<ProgHdr64*>(reinterpret_cast<uint8_t*>(elf) + static_cast<uint32_t>(elf->e_phoff));
    auto* eph = ph + elf->e_phnum;

    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }

        auto phys_addr = static_cast<uint32_t>(ph->p_pa);
        auto* dst = reinterpret_cast<uint8_t*>(phys_addr);
        auto* src = reinterpret_cast<uint8_t*>(elf) + static_cast<uint32_t>(ph->p_offset);

        if (phys_addr < bi->kernel_start) {
            bi->kernel_start = phys_addr;
        }
        if (phys_addr + static_cast<uint32_t>(ph->p_memsz) > bi->kernel_end) {
            bi->kernel_end = phys_addr + static_cast<uint32_t>(ph->p_memsz);
        }

        memcpy(dst, src, static_cast<uint32_t>(ph->p_filesz));

        // Zero BSS
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + static_cast<uint32_t>(ph->p_filesz), 0, static_cast<uint32_t>(ph->p_memsz - ph->p_filesz));
        }
    }

    return 0;
}

static uint32_t fat_find_file(const char* filename, uint8_t* dir_buffer, uint32_t root_entries) {
    auto* entry = reinterpret_cast<FatDirEntry*>(dir_buffer);

    for (uint32_t i = 0; i < root_entries; i++) {
        if (entry[i].is_end()) {
            break;
        }
        if (entry[i].is_deleted()) {
            continue;
        }
        // Compare name (8 bytes) and ext (3 bytes) together as 11 bytes
        if (memcmp(&entry[i].name, filename, 11) == 0) {
            return entry[i].get_cluster();
        }
    }
    return 0;  // Not found
}

static int fat_load_file(const char* filename, uint8_t* buffer) {
    if (bpb32->common.fat_size_16 != 0)
        return -1;

    uint8_t sectors_per_cluster = bpb32->common.sectors_per_cluster;
    uint32_t fat_start = bpb32->common.reserved_sectors;
    uint32_t data_start = fat_start + (bpb32->common.num_fats * bpb32->fat_size_32);
    uint32_t root_start = data_start + (bpb32->root_cluster - 2) * sectors_per_cluster;

    auto* root_dir = reinterpret_cast<uint8_t*>(0x8C00);
    if (read_sectors(root_start, sectors_per_cluster, root_dir) != 0)
        return -1;
    uint32_t root_entries = (sectors_per_cluster * SECT_SIZE) / 32;

    uint32_t cluster = fat_find_file(filename, root_dir, root_entries);
    if (cluster == 0)
        return -1;

    auto* fat = reinterpret_cast<uint32_t*>(0x8C00);
    if (read_sectors(fat_start, bpb32->fat_size_32, reinterpret_cast<uint8_t*>(fat)) != 0)
        return -1;

    uint8_t* dest = buffer;
    while (cluster < 0x0FFFFFF8) {
        uint32_t sector = data_start + (cluster - 2) * sectors_per_cluster;

        if (read_sectors(sector, sectors_per_cluster, dest) != 0)
            return -1;

        dest += sectors_per_cluster * SECT_SIZE;
        cluster = fat[cluster];
    }

    return 0;
}

static void build_page_tables() {
    auto* pml4 = reinterpret_cast<uint32_t*>(BOOT_PML4_ADDR);
    auto* pdpt = reinterpret_cast<uint32_t*>(BOOT_PDPT_ADDR);
    auto* pd0 = reinterpret_cast<uint32_t*>(BOOT_PD0_ADDR);
    auto* pd1 = reinterpret_cast<uint32_t*>(BOOT_PD1_ADDR);

    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd0, 0, 4096);
    memset(pd1, 0, 4096);

    // PML4[0] -> PDPT (each entry is 8 bytes: low dword + high dword)
    pml4[0] = BOOT_PDPT_ADDR | PT_P | PT_W;
    pml4[1] = 0;

    // PDPT[0] -> PD0 (maps 0..1GB)
    pdpt[0] = BOOT_PD0_ADDR | PT_P | PT_W;
    pdpt[1] = 0;

    // PDPT[1] -> PD1 (maps 1..2GB)
    pdpt[2] = BOOT_PD1_ADDR | PT_P | PT_W;
    pdpt[3] = 0;

    // Fill PD0: 512 x 2MB pages mapping 0..1GB
    for (uint32_t i = 0; i < 512; i++) {
        uint32_t phys = i * 0x200000;  // i * 2MB
        pd0[i * 2] = phys | PT_P | PT_W | PT_PS;
        pd0[i * 2 + 1] = 0;  // high dword = 0 (below 4GB)
    }

    // Fill PD1: 512 x 2MB pages mapping 1GB..2GB
    for (uint32_t i = 0; i < 512; i++) {
        uint32_t phys = 0x40000000 + i * 0x200000;  // 1GB + i * 2MB
        pd1[i * 2] = phys | PT_P | PT_W | PT_PS;
        pd1[i * 2 + 1] = 0;
    }
}

// =============================================================================
// Build 64-bit GDT at BOOT_GDT64_ADDR
// =============================================================================
static void build_gdt64() {
    auto* gdt = reinterpret_cast<uint64_t*>(BOOT_GDT64_ADDR);

    gdt[0] = 0;

    // Entry 1: 64-bit code segment (L=1, D=0, P=1, DPL=0, S=1, Type=Execute/Read)
    // Bytes: limit_lo=0xFFFF, base_lo=0x0000, base_mid=0x00,
    //        access=0x9A (P=1,DPL=0,S=1,Type=0xA=exec/read),
    //        flags_limit=0xAF (G=1,L=1,D=0,limit_hi=0xF), base_hi=0x00
    gdt[1] = 0x00AF9A000000FFFFULL;

    // Entry 2: 64-bit data segment (P=1, DPL=0, S=1, Type=Read/Write)
    gdt[2] = 0x00CF92000000FFFFULL;

    // GDT descriptor at BOOT_GDTDESC64_ADDR
    // Format: 2 bytes limit, 4 bytes base (for 32-bit lgdt)
    auto* gdtdesc = reinterpret_cast<uint16_t*>(BOOT_GDTDESC64_ADDR);
    gdtdesc[0] = 3 * 8 - 1;  // limit: 3 entries * 8 bytes - 1
    auto* gdtdesc_base = reinterpret_cast<uint32_t*>(BOOT_GDTDESC64_ADDR + 2);
    *gdtdesc_base = BOOT_GDT64_ADDR;
}

// =============================================================================
// enter_long_mode: defined in enter_long_mode.S
// Transitions from 32-bit protected mode to 64-bit long mode and jumps to kernel.
// Never returns.
//   kernel_entry_phys: physical address of kernel entry point
//   info: pointer to struct boot_info (passed to kernel in %rdi)
// =============================================================================
extern "C" {
[[noreturn]] void enter_long_mode(uint32_t kernel_entry_phys, struct BootInfo* info);

[[gnu::section(".text.bootmain")]] void bootmain(uint32_t boot_drive_param);
}

void bootmain(uint32_t boot_drive_param) {
    boot_drive = static_cast<uint8_t>(boot_drive_param);

    memset(&bi, 0, sizeof(bi));
    bi.magic = BOOT_INFO_MAGIC;

    uint32_t e820_count = *reinterpret_cast<uint32_t*>(E820_MEM_BASE);
    bi.mmap_length = e820_count;
    bi.mmap_addr = E820_MEM_DATA;

    auto* mmap_entries = reinterpret_cast<BootMemEntry*>(E820_MEM_DATA);
    bi.mem_lower = 640;  // Always 640KB
    for (uint32_t i = 0; i < e820_count; i++) {
        auto* entry = &mmap_entries[i];
        if (entry->type == E820_RAM && entry->addr >= 0x100000) {
            bi.mem_upper += (entry->len >> 10);
        }
    }

    strcpy(bi.loader_name, "Zonix BIOS");

    if (fat_load_file(KERNEL_FNAME, reinterpret_cast<uint8_t*>(KERNEL_ELF_ADDR)) != 0) {
        goto bad;
    }

    if (load_elf_kernel(reinterpret_cast<uint8_t*>(KERNEL_ELF_ADDR), &bi) != 0) {
        goto bad;
    }

    {
        uint32_t kernel_entry_phys = static_cast<uint32_t>(bi.kernel_entry & 0x7FFFFFFF);

        build_page_tables();
        build_gdt64();

        enter_long_mode(kernel_entry_phys, &bi);  // in entry.S
    }

bad:
    while (true)
        __asm__ volatile("hlt");
}
