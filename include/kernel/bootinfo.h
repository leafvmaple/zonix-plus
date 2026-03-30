#pragma once

#include <base/types.h>

inline constexpr uint32_t BOOT_INFO_MAGIC = 0x12345678;

inline constexpr uint32_t BOOT_MEM_AVAILABLE = 1;  // Usable RAM
inline constexpr uint32_t BOOT_MEM_RESERVED  = 2;  // Reserved
inline constexpr uint32_t BOOT_MEM_ACPI      = 3;  // ACPI reclaimable
inline constexpr uint32_t BOOT_MEM_NVS       = 4;  // ACPI NVS
inline constexpr uint32_t BOOT_MEM_BAD       = 5;  // Bad memory

struct BootMemEntry {
    uint64_t addr;      // Physical address
    uint64_t len;       // Length in bytes
    uint32_t type;      // Memory type (BOOT_MEM_*)
} __attribute__((packed));

struct BootInfo {
    uint32_t magic;                  // Must be BOOT_INFO_MAGIC

    uint32_t mem_lower;              // Lower memory in KB (0-640KB)
    uint32_t mem_upper;              // Upper memory in KB (1MB+)
    uint32_t mmap_length;            // Memory map length (number of entries)
    uint64_t mmap_addr;              // Physical address of BootMemEntry array

    uint32_t kernel_start;           // Kernel physical start address
    uint32_t kernel_end;             // Kernel physical end address
    uint32_t kernel_entry;           // Kernel entry point (virtual)

    uint8_t  boot_device;            // Boot drive number (BIOS)

    uint64_t cmdline_addr;           // Physical address of command line string
    
    // Framebuffer (for UEFI support)
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;       // 0=text, 1=rgb

    char     loader_name[32];        // "Zonix BIOS" or "Zonix UEFI"
} __attribute__((packed));

using kernel_entry_t = void (*)(BootInfo *info);
