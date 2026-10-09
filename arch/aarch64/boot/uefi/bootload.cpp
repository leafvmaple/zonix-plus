#include "kernel/bootinfo.h"
#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define SAFE_BOOT_INFO_ADDR   0x40005000ULL
#define SAFE_MMAP_ADDR        0x40005100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x40007000ULL - SAFE_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = uefi_string(L"\r\nZonix UEFI Bootloader (AArch64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI",
        .kernel_base_va = 0xFFFF000000000000ULL,
        .boot_info_pa = SAFE_BOOT_INFO_ADDR,
        .memory_map_pa = SAFE_MMAP_ADDR,
        .memory_map_capacity = SAFE_MMAP_MAX_ENTRIES,
        .lower_memory_kib = 0,
        .upper_memory_start_pa = 0,
        .kernel_alloc_pa = 0x40080000ULL,
        .kernel_page_count = 256, /* 1 MB */
    };

    BootInfo* bi = nullptr;
    EFI_STATUS status = uefi_boot_setup(image_handle, system_table, cfg, &bi);
    if (EFI_ERROR(status)) {
        return status;
    }

    /*
     * Jump to kernel.  AArch64 UEFI and kernel both use AAPCS64
     * (first argument in x0), so a direct function call suffices.
     */
    auto entry = reinterpret_cast<KernelEntry>(bi->kernel_entry);
    entry(bi);

    for (;;) {
        __asm__ volatile("wfe");
    }
    return EFI_SUCCESS;
}
