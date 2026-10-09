#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define SAFE_BOOT_INFO_ADDR   0x5000ULL
#define SAFE_MMAP_ADDR        0x5100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x7000ULL - SAFE_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = uefi_string(L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI",
        .kernel_base_va = 0xFFFFFFFF80000000ULL,
        .boot_info_pa = SAFE_BOOT_INFO_ADDR,
        .memory_map_pa = SAFE_MMAP_ADDR,
        .memory_map_capacity = SAFE_MMAP_MAX_ENTRIES,
        .lower_memory_kib = 640,
        .upper_memory_start_pa = 0x100000,
        .kernel_alloc_pa = 0,
        .kernel_page_count = 0,
    };

    BootInfo* bi = nullptr;
    EFI_STATUS status = uefi_boot_setup(image_handle, system_table, cfg, &bi);
    if (EFI_ERROR(status)) {
        return status;
    }

    /*
     * Jump to kernel entry.
     * This UEFI binary uses the Microsoft x64 ABI (first arg in %rcx)
     * but the kernel expects System V ABI (first arg in %rdi).
     * Use inline assembly to place boot_info in %rdi explicitly.
     */
    uint64_t entry_addr = static_cast<uint64_t>(bi->kernel_entry);
    uint64_t info_addr = reinterpret_cast<uint64_t>(bi);
    __asm__ volatile("movq %0, %%rdi\n\t"
                     "jmp *%1\n\t"
                     :
                     : "r"(info_addr), "r"(entry_addr)
                     : "rdi", "memory");

    return EFI_SUCCESS;
}
