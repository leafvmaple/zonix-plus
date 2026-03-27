#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define KERNEL_VIRT_BASE 0xFFFF000000000000ULL

/*
 * QEMU virt RAM starts at 0x4000_0000; kernel loads at 0x4008_0000.
 * Place boot_info and mmap well below the kernel.
 */
#define SAFE_BOOT_INFO_ADDR   0x40005000ULL
#define SAFE_MMAP_ADDR        0x40005100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x40007000ULL - SAFE_MMAP_ADDR) / sizeof(boot_mmap_entry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    EFI_SYSTEM_TABLE* st = system_table;
    EFI_BOOT_SERVICES* bs = system_table->BootServices;

    st->ConOut->ClearScreen(st->ConOut);
    uefi_print(st, UEFI_STR(L"\r\nZonix UEFI Bootloader (AArch64) v1.0\r\n\r\n"));
    bs->SetWatchdogTimer(0, 0, 0, 0);

    auto* bi = (boot_info*)(UINTN)SAFE_BOOT_INFO_ADDR;
    memset(bi, 0, sizeof(boot_info));
    bi->magic = BOOT_INFO_MAGIC;
    bi->mmap_addr = SAFE_MMAP_ADDR;

    uefi_print(st, UEFI_STR(L"Getting memory map...\r\n"));
    EFI_STATUS status = uefi_get_memory_map(bs, bi, SAFE_MMAP_ADDR, SAFE_MMAP_MAX_ENTRIES, 0, 0);
    if (EFI_ERROR(status)) {
        uefi_print(st, UEFI_STR(L"Memory map failed\r\n"));
        return status;
    }

    uefi_print(st, UEFI_STR(L"Loading kernel...\r\n"));
    VOID* kernel_buf = 0;
    UINTN kernel_size = 0;
    status = uefi_load_kernel_file(bs, image_handle, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        uefi_print(st, UEFI_STR(L"Kernel load failed\r\n"));
        return status;
    }

    uefi_print(st, UEFI_STR(L"Parsing ELF kernel...\r\n"));

    /*
     * EDK2 on AArch64 marks EfiConventionalMemory as non-executable.
     * Claim the kernel's load region as EfiLoaderCode before writing.
     */
    {
        EFI_PHYSICAL_ADDRESS kernel_base = 0x40080000ULL;
        UINTN kernel_pages = 256; /* 1 MB */
        status = bs->AllocatePages(AllocateAddress, EfiLoaderCode, kernel_pages, &kernel_base);
        if (EFI_ERROR(status)) {
            uefi_print(st, UEFI_STR(L"AllocatePages for kernel failed\r\n"));
            return status;
        }
    }

    if (uefi_load_elf(kernel_buf, bi, KERNEL_VIRT_BASE) != 0) {
        uefi_print(st, UEFI_STR(L"ELF parse failed\r\n"));
        return EFI_LOAD_ERROR;
    }

    uefi_get_graphics_info(bs, bi);

    const char* name = "Zonix UEFI";
    for (int i = 0; name[i] && i < 31; i++) {
        bi->loader_name[i] = name[i];
    }

    uefi_print(st, UEFI_STR(L"Kernel entry (phys): "));
    uefi_print_hex(st, bi->kernel_entry);

    uefi_print(st, UEFI_STR(L"Exiting Boot Services...\r\n"));
    status = uefi_exit_boot_services(bs, image_handle);
    if (EFI_ERROR(status)) {
        return status;
    }

    /*
     * Jump to kernel.  AArch64 UEFI and kernel both use AAPCS64
     * (first argument in x0), so a direct function call suffices.
     */
    using kernel_entry_fn = void (*)(boot_info*);
    auto entry = (kernel_entry_fn)(UINTN)bi->kernel_entry;
    entry(bi);

    for (;;) {
        __asm__ volatile("wfe");
    }
    return EFI_SUCCESS;
}
