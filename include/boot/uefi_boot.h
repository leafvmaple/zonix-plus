#pragma once

#include <uefi/uefi.h>
#include <kernel/bootinfo.h>

struct UefiBootConfig {
    const wchar_t* banner;    // e.g. L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n"
    const char* loader_name;  // e.g. "Zonix UEFI"
    uint64_t kernel_virt_base;
    uint64_t boot_info_addr;  // physical address for BootInfo
    uint64_t mmap_addr;       // physical address for memory map
    uintptr_t mmap_max_entries;
    uint32_t mem_lower;                      // lower memory in KB (x86: 640, others: 0)
    uint64_t mem_upper_min;                  // min addr for upper memory accounting
    EFI_PHYSICAL_ADDRESS kernel_alloc_base;  // 0 to skip AllocatePages
    uintptr_t kernel_alloc_pages;            // number of pages to allocate
};

EFI_STATUS uefi_boot_setup(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table, const UefiBootConfig& cfg,
                           BootInfo** out_bi);
