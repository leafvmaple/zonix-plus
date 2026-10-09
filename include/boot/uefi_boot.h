#pragma once

#include <uefi/uefi.h>
#include <kernel/bootinfo.h>

struct UefiBootConfig {
    const wchar_t* banner;    // e.g. L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n"
    const char* loader_name;  // e.g. "Zonix UEFI"
    uint64_t kernel_base_va;
    uint64_t boot_info_pa;   // physical address for BootInfo
    uint64_t memory_map_pa;  // physical address for memory map
    uintptr_t memory_map_capacity;
    uint32_t lower_memory_kib;             // lower memory in KB (x86: 640, others: 0)
    uint64_t upper_memory_start_pa;        // min addr for upper memory accounting
    EFI_PHYSICAL_ADDRESS kernel_alloc_pa;  // 0 to skip AllocatePages
    uintptr_t kernel_page_count;           // number of pages to allocate
};

EFI_STATUS uefi_boot_setup(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table, const UefiBootConfig& config,
                           BootInfo** out_boot_info);
