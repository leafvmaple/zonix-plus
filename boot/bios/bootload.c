// BIOS Bootloader for Zonix
// Loaded by VBR at 0x7E00, runs in 32-bit protected mode
// This bootloader:
//   1. Reads FAT16/FAT32 filesystem from disk using ATA PIO ports
//   2. Locates and loads KERNEL.SYS (ELF file) to 0x10000
//   3. Parses ELF and loads segments to their proper locations
//   4. Jumps to kernel entry point

#include <base/elf.h>
#include <base/types.h>
#include <kernel/bootinfo.h>
#include <base/bpb.h>

#include <arch/x86/asm/seg.h>
#include <arch/x86/io.h>

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

// Load ELF kernel
static int load_elf_kernel(uint8_t* elf_buffer, struct boot_info* boot_info) {
    elfhdr* elf = (elfhdr*)elf_buffer;
    
    // Verify ELF magic
    if (elf->e_magic != ELF_MAGIC) {
        return -1;
    }
    
    // Track kernel boundaries
    boot_info->kernel_start = 0xFFFFFFFF;
    boot_info->kernel_end = 0;
    boot_info->kernel_entry = elf->e_entry;
    
    // Load each program segment
    proghdr* ph = (proghdr*)((uint8_t*)elf + elf->e_phoff);
    proghdr* eph = ph + elf->e_phnum;
    
    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD)
            continue;
        
        // Physical address (mask off virtual kernel base 0xC0000000)
        uint8_t* dst = (uint8_t*)(ph->p_va & 0x00FFFFFF);
        uint8_t* src = (uint8_t*)elf + ph->p_offset;
        
        // Track kernel boundaries
        uint32_t phys_addr = (uint32_t)dst;
        if (phys_addr < boot_info->kernel_start)
            boot_info->kernel_start = phys_addr;
        if (phys_addr + ph->p_memsz > boot_info->kernel_end)
            boot_info->kernel_end = phys_addr + ph->p_memsz;
        
        // Copy segment
        memcpy(dst, src, ph->p_filesz);
        
        // Zero BSS
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
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
            return ((uint32_t)entry[i].first_cluster_hi << 16) | entry[i].first_cluster_lo;
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

void __attribute__((section(".text.bootmain"))) bootmain(uint32_t boot_drive_param) {
    boot_drive = (uint8_t)boot_drive_param;
    
    // Prepare boot_info structure
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.magic = BOOT_INFO_MAGIC;
    
    // Copy E820 memory map (set by VBR during E820 probe)
    uint32_t e820_count = *(uint32_t*)E820_MEM_BASE;
    boot_info.mmap_length = e820_count;
    boot_info.mmap = (struct boot_mmap_entry*)E820_MEM_DATA;
    
    // Calculate memory sizes from E820 map
    boot_info.mem_lower = 640;  // Always 640KB
    for (uint32_t i = 0; i < e820_count; i++) {
        struct boot_mmap_entry* entry = &boot_info.mmap[i];
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
    
    // Jump to kernel entry point
    kernel_entry_t kernel_entry = (kernel_entry_t)(boot_info.kernel_entry & 0x00FFFFFF);
    kernel_entry(&boot_info);
    
bad:
    while (1)
        __asm__ volatile("hlt");
}
