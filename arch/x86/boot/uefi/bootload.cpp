#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define SAFE_BOOT_INFO_ADDR   0x5000ULL
#define SAFE_MMAP_ADDR        0x5100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x7000ULL - SAFE_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = UEFI_STR(L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI",
        .kernel_virt_base = 0xFFFFFFFF80000000ULL,
        .boot_info_addr = SAFE_BOOT_INFO_ADDR,
        .mmap_addr = SAFE_MMAP_ADDR,
        .mmap_max_entries = SAFE_MMAP_MAX_ENTRIES,
        .mem_lower = 640,
        .mem_upper_min = 0x100000,
        .kernel_alloc_base = 0,
        .kernel_alloc_pages = 0,
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
