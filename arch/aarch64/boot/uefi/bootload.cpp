#include "kernel/bootinfo.h"
#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define SAFE_BOOT_INFO_ADDR   0x40005000ULL
#define SAFE_MMAP_ADDR        0x40005100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x40007000ULL - SAFE_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = UEFI_STR(L"\r\nZonix UEFI Bootloader (AArch64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI",
        .kernel_virt_base = 0xFFFF000000000000ULL,
        .boot_info_addr = SAFE_BOOT_INFO_ADDR,
        .mmap_addr = SAFE_MMAP_ADDR,
        .mmap_max_entries = SAFE_MMAP_MAX_ENTRIES,
        .mem_lower = 0,
        .mem_upper_min = 0,
        .kernel_alloc_base = 0x40080000ULL,
        .kernel_alloc_pages = 256, /* 1 MB */
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
    using kernel_entry_fn = void (*)(BootInfo*);
    auto entry = reinterpret_cast<kernel_entry_fn>(bi->kernel_entry);
    entry(bi);

    for (;;) {
        __asm__ volatile("wfe");
    }
    return EFI_SUCCESS;
}
