// BIOS Bootloader for Zonix
// Loaded by VBR at 0x7E00, runs in 32-bit protected mode
// This bootloader:
//   1. Reads FAT16/FAT32 filesystem from disk using ATA PIO ports
//   2. Locates and loads KERNEL.SYS (ELF file) to 0x10000
//   3. Parses ELF and loads segments to their proper locations
//   4. Sets up 64-bit long mode (PAE, PML4 page tables, EFER, paging)
//   5. Jumps to kernel entry point in 64-bit mode

#include <base/elf.h>
#include <base/types.h>
#include <kernel/bootinfo.h>
#include <base/bpb.h>

#include <asm/seg.h>
#include <asm/cr.h>
#include <asm/io.h>

#define SECT_SIZE 512
#define KERNEL_NAME "KERNEL  SYS"

// ATA PIO ports (Primary IDE controller)
#define ATA_PORT_DATA       0x1F0    // Data register
#define ATA_PORT_ERROR      0x1F1    // Error register (read)
#define ATA_PORT_FEATURES   0x1F1    // Features register (write)
#define ATA_PORT_SECTOR_CNT 0x1F2    // Sector count
#define ATA_PORT_LBA_LOW    0x1F3    // LBA low byte
#define ATA_PORT_LBA_MID    0x1F4    // LBA mid byte
#define ATA_PORT_LBA_HIGH   0x1F5    // LBA high byte
#define ATA_PORT_DRIVE      0x1F6    // Drive/Head register
#define ATA_PORT_STATUS     0x1F7    // Status register (read)
#define ATA_PORT_COMMAND    0x1F7    // Command register (write)

// ATA commands
#define ATA_CMD_READ_PIO    0x20     // Read with retry
#define ATA_CMD_READ_PIO_EXT 0x24    // Read PIO extended (48-bit LBA)

// ATA status bits
#define ATA_STATUS_BSY      0x80     // Busy
#define ATA_STATUS_DRDY     0x40     // Drive ready
#define ATA_STATUS_DRQ      0x08     // Data request ready
#define ATA_STATUS_ERR      0x01     // Error

#define KERNEL_ELF_ADDR 0x10000

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
#define BOOT_PML4_ADDR  0x1000
#define BOOT_PDPT_ADDR  0x2000
#define BOOT_PD0_ADDR   0x3000
#define BOOT_PD1_ADDR   0x4000
#define BOOT_GDT64_ADDR 0x6000
#define BOOT_GDTDESC64_ADDR 0x6040

// Page table entry flags
#define PT_P    0x001   // Present
#define PT_W    0x002   // Writable
#define PT_PS   0x080   // Page Size (2MB)

// EFER MSR
#define MSR_EFER_ADDR  0xC0000080
#define EFER_LME_BIT   0x100

// Global variables
static uint8_t boot_drive;
static fat32_bpb_t* bpb32 = (fat32_bpb_t*)0x7C0B;  // BPB starts at 0x7C00 + 0x0B (after jmp + oem)
static struct boot_info boot_info;

// Simple memcpy
static void* memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

// Simple memset
static void* memset(void* dst, int c, size_t n) {
    char* d = (char*)dst;
    while (n--)
        *d++ = (char)c;
    return dst;
}

static void* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

// Compare memory
static int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// Wait for ATA drive to be ready (not busy)
static void ata_wait_ready(void) {
    // Wait until BSY=0 and DRDY=1 (status & 0xC0 == 0x40)
    while ((inb(ATA_PORT_STATUS) & 0xC0) != 0x40)
        ;
}

// Read one sector using ATA PIO mode (LBA28)
// lba: absolute LBA sector number
// buffer: must be 512-byte aligned
static int ata_read_sector(uint32_t lba, void* buffer) {
    uint16_t* buf = (uint16_t*)buffer;
    
    // Wait for drive to be ready
    ata_wait_ready();

    outb(0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F6, ((lba >> 24) & 0xF) | 0xE0);
    outb(0x1F7, 0x20);
    
    // Wait for data to be ready
    ata_wait_ready();

    insl(0x1F0, buffer, SECT_SIZE >> 2);  // Read 512 bytes (256 words)
    
    return 0;
}

// Read multiple sectors
static int read_sectors(uint32_t lba, uint32_t count, uint8_t* buffer) {
    for (uint32_t i = 0; i < count; i++) {
        // Calculate absolute LBA: hidden_sectors + 1 (VBR) + lba
        uint32_t abs_lba = bpb32->common.hidden_sectors + 1 + lba + i;
        if (ata_read_sector(abs_lba, buffer + i * SECT_SIZE) != 0)
            return -1;
    }
    return 0;
}

// Load ELF64 kernel
static int load_elf_kernel(uint8_t* elf_buffer, struct boot_info* boot_info) {
    elfhdr64* elf = (elfhdr64*)elf_buffer;
    
    // Verify ELF magic
    if (elf->e_magic != ELF_MAGIC) {
        return -1;
    }
    
    // Track kernel boundaries
    boot_info->kernel_start = 0xFFFFFFFF;
    boot_info->kernel_end = 0;
    boot_info->kernel_entry = (uint32_t)(elf->e_entry & 0xFFFFFFFF);  // Physical entry
    
    // Load each program segment (ELF64 program headers)
    proghdr64* ph = (proghdr64*)((uint8_t*)elf + (uint32_t)elf->e_phoff);
    proghdr64* eph = ph + elf->e_phnum;
    
    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD)
            continue;
        
        // Use physical address from ELF program header directly
        uint32_t phys_addr = (uint32_t)ph->p_pa;
        uint8_t* dst = (uint8_t*)phys_addr;
        uint8_t* src = (uint8_t*)elf + (uint32_t)ph->p_offset;
        
        // Track kernel boundaries
        if (phys_addr < boot_info->kernel_start)
            boot_info->kernel_start = phys_addr;
        if (phys_addr + (uint32_t)ph->p_memsz > boot_info->kernel_end)
            boot_info->kernel_end = phys_addr + (uint32_t)ph->p_memsz;
        
        // Copy segment
        memcpy(dst, src, (uint32_t)ph->p_filesz);
        
        // Zero BSS
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + (uint32_t)ph->p_filesz, 0, (uint32_t)(ph->p_memsz - ph->p_filesz));
        }
    }

    return 0;
}

// Find file in FAT root directory
// Returns starting cluster number, or 0 if not found
static uint32_t fat_find_file(const char* filename, uint8_t* dir_buffer, uint32_t root_entries) {
    fat_dir_entry_t* entry = (fat_dir_entry_t*)dir_buffer;
    
    for (uint32_t i = 0; i < root_entries; i++) {
        if (entry[i].name[0] == 0x00)  // No more entries
            break;
        if (entry[i].name[0] == 0xE5)  // Deleted entry
            continue;
        // Compare name (8 bytes) and ext (3 bytes) together as 11 bytes
        if (memcmp(&entry[i].name, filename, 11) == 0) {
            // For FAT32, combine hi and lo parts of cluster number
            return ((uint32_t)entry[i].first_cluster_high << 16) | entry[i].first_cluster_low;
        }
    }
    return 0;  // Not found
}

// Load file from FAT filesystem
// Returns 0 on success, -1 on error
static int fat_load_file(const char* filename, uint8_t* buffer) {
    if (bpb32->common.fat_size_16 != 0)
        return -1;

    uint8_t sectors_per_cluster = bpb32->common.sectors_per_cluster;
    uint32_t fat_start          = bpb32->common.reserved_sectors;
    uint32_t data_start         = fat_start + (bpb32->common.num_fats * bpb32->fat_size_32);
    uint32_t root_start         = data_start + (bpb32->root_cluster - 2) * sectors_per_cluster;
    
    // Load root directory to temporary buffer at 0x8E00
    uint8_t* root_dir = (uint8_t*)0x8C00;
    // read 1 cluster. from root sectors
    if (read_sectors(root_start, sectors_per_cluster, root_dir) != 0)
        return -1;
    uint32_t root_entries = (sectors_per_cluster * SECT_SIZE) / 32;
    
    // Find file in root directory
    uint32_t cluster = fat_find_file(filename, root_dir, root_entries);
    if (cluster == 0)
        return -1;  // File not found
    
    // Load FAT table to 0x8E00 (reuse root_dir buffer)
    uint32_t* fat = (uint32_t*)0x8C00;
    if (read_sectors(fat_start, bpb32->fat_size_32, (uint8_t*)fat) != 0)
        return -1;
    
    // Load file clusters
    uint8_t* dest = buffer;
    while (cluster < 0x0FFFFFF8) {
        // Calculate sector for this cluster
        uint32_t sector = data_start + (cluster - 2) * sectors_per_cluster;
        
        // Read cluster
        if (read_sectors(sector, sectors_per_cluster, dest) != 0)
            return -1;
        
        // Move to next cluster
        dest += sectors_per_cluster * SECT_SIZE;
        cluster = fat[cluster];
    }
    
    return 0;
}

// =============================================================================
// Build identity-mapped page tables for 32->64 transition
// Maps first 2GB with 2MB pages (enough for kernel loading area)
// =============================================================================
static void build_page_tables(void) {
    uint32_t *pml4 = (uint32_t*)BOOT_PML4_ADDR;
    uint32_t *pdpt = (uint32_t*)BOOT_PDPT_ADDR;
    uint32_t *pd0  = (uint32_t*)BOOT_PD0_ADDR;
    uint32_t *pd1  = (uint32_t*)BOOT_PD1_ADDR;

    // Clear all page table pages
    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd0,  0, 4096);
    memset(pd1,  0, 4096);

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
        pd0[i * 2]     = phys | PT_P | PT_W | PT_PS;
        pd0[i * 2 + 1] = 0;  // high dword = 0 (below 4GB)
    }

    // Fill PD1: 512 x 2MB pages mapping 1GB..2GB
    for (uint32_t i = 0; i < 512; i++) {
        uint32_t phys = 0x40000000 + i * 0x200000;  // 1GB + i * 2MB
        pd1[i * 2]     = phys | PT_P | PT_W | PT_PS;
        pd1[i * 2 + 1] = 0;
    }
}

// =============================================================================
// Build 64-bit GDT at BOOT_GDT64_ADDR
// =============================================================================
static void build_gdt64(void) {
    uint64_t *gdt = (uint64_t*)BOOT_GDT64_ADDR;

    // Entry 0: NULL descriptor
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
    uint16_t *gdtdesc = (uint16_t*)BOOT_GDTDESC64_ADDR;
    gdtdesc[0] = 3 * 8 - 1;        // limit: 3 entries * 8 bytes - 1
    uint32_t *gdtdesc_base = (uint32_t*)(BOOT_GDTDESC64_ADDR + 2);
    *gdtdesc_base = BOOT_GDT64_ADDR;
}

// =============================================================================
// Enter 64-bit long mode and jump to kernel
//
// This function NEVER returns. It:
//   1. Enables PAE in CR4
//   2. Loads PML4 into CR3
//   3. Enables Long Mode in EFER MSR
//   4. Enables Paging in CR0 (activating long mode)
//   5. Loads 64-bit GDT
//   6. Far-jumps to 64-bit code
//   7. In 64-bit mode: sets segments, loads boot_info into %rdi, jumps to kernel
// =============================================================================
static void __attribute__((noreturn)) enter_long_mode(uint32_t kernel_entry_phys, struct boot_info *info) {
    // Build page tables and GDT
    build_page_tables();
    build_gdt64();

    // The transition code is in inline assembly because we're switching
    // from 32-bit to 64-bit mode
    __asm__ volatile (
        // Save parameters in registers that won't be clobbered
        "movl %0, %%ebx\n\t"       // ebx = kernel_entry_phys
        "movl %1, %%esi\n\t"       // esi = boot_info pointer

        // 1. Enable PAE (CR4.PAE = bit 5)
        "movl %%cr4, %%eax\n\t"
        "orl  $0x20, %%eax\n\t"    // CR4_PAE
        "movl %%eax, %%cr4\n\t"

        // 2. Load PML4 into CR3
        "movl %2, %%eax\n\t"       // BOOT_PML4_ADDR
        "movl %%eax, %%cr3\n\t"

        // 3. Enable Long Mode (EFER.LME = bit 8)
        "movl $0xC0000080, %%ecx\n\t"  // MSR_EFER
        "rdmsr\n\t"
        "orl  $0x100, %%eax\n\t"       // EFER_LME
        "wrmsr\n\t"

        // 4. Enable Paging (CR0.PG = bit 31) and Write Protect (CR0.WP = bit 16)
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80010000, %%eax\n\t"  // CR0_PG | CR0_WP
        "movl %%eax, %%cr0\n\t"

        // 5. Load 64-bit GDT
        "lgdt %3\n\t"

        // 6. Far jump to 64-bit code segment
        //    GD_KTEXT = 0x08 (selector for entry 1)
        //    Use ljmp with absolute address to the .Llong_mode label
        "ljmp $0x08, $.Llong_mode\n\t"

        // 7. 64-bit code
        ".code64\n\t"
        ".Llong_mode:\n\t"

        // Set up 64-bit data segments (GD_KDATA = 0x10, entry 2)
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"

        // Set up a temporary stack in 64-bit mode
        "movq $0x7000, %%rsp\n\t"

        // Load boot_info pointer into %rdi (x86_64 calling convention, first argument)
        // %esi was saved earlier (32-bit), zero-extend to 64-bit
        "movl %%esi, %%edi\n\t"

        // Jump to kernel entry point (physical address in %ebx, zero-extended)
        "movl %%ebx, %%eax\n\t"
        "jmp *%%rax\n\t"

        // Switch back to 32-bit assembly mode for the rest of this file
        ".code32\n\t"

        :
        : "r"(kernel_entry_phys),
          "r"(info),
          "i"(BOOT_PML4_ADDR),
          "m"(*(char*)BOOT_GDTDESC64_ADDR)
        : "eax", "ebx", "ecx", "edx", "esi", "memory"
    );

    __builtin_unreachable();
}

void __attribute__((section(".text.bootmain"))) bootmain(uint32_t boot_drive_param) {
    boot_drive = (uint8_t)boot_drive_param;
    
    // Prepare boot_info structure
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.magic = BOOT_INFO_MAGIC;
    
    // Copy E820 memory map (set by VBR during E820 probe)
    uint32_t e820_count = *(uint32_t*)E820_MEM_BASE;
    boot_info.mmap_length = e820_count;
    boot_info.mmap_addr = E820_MEM_DATA;
    
    // Calculate memory sizes from E820 map
    struct boot_mmap_entry* mmap_entries = (struct boot_mmap_entry*)E820_MEM_DATA;
    boot_info.mem_lower = 640;  // Always 640KB
    for (uint32_t i = 0; i < e820_count; i++) {
        struct boot_mmap_entry* entry = &mmap_entries[i];
        if (entry->type == E820_RAM && entry->addr >= 0x100000) {
            boot_info.mem_upper += (entry->len >> 10);
        }
    }

    // Set loader name
    strcpy(boot_info.loader_name, "Zonix BIOS");
    
    // Load KERNEL.SYS from FAT filesystem
    if (fat_load_file(KERNEL_NAME, (uint8_t*)KERNEL_ELF_ADDR) != 0) {
        goto bad;
    }
    
    // Load ELF kernel
    if (load_elf_kernel((uint8_t*)KERNEL_ELF_ADDR, &boot_info) != 0) {
        goto bad;
    }
    
    // Compute physical entry point:
    // The ELF entry is a virtual address like 0xFFFFFFFF80100000
    // Strip the higher-half base (0xFFFFFFFF80000000) to get physical address
    uint32_t kernel_entry_phys = (uint32_t)(boot_info.kernel_entry & 0x7FFFFFFF);

    // Enter 64-bit long mode and jump to kernel
    // This function never returns
    enter_long_mode(kernel_entry_phys, &boot_info);
    
bad:
    while (1)
        __asm__ volatile("hlt");
}
