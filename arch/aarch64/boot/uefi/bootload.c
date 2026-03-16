/**
 * @file bootload.c
 * @brief Zonix UEFI bootloader — AArch64 platform layer.
 *
 * Common UEFI boot logic lives in <boot/uefi_boot.h>.
 * This file provides only AArch64-specific constants and the kernel jump.
 */

#include <uefi/uefi.h>

/* Global UEFI state (referenced by uefi_boot.h helpers) */
static EFI_SYSTEM_TABLE* ST;
static EFI_BOOT_SERVICES* BS;

/* ---- Platform configuration ---- */

#define KERNEL_VIRT_BASE 0xFFFF000000000000ULL

/*
 * Safe fixed physical addresses for boot data.
 * QEMU virt RAM starts at 0x4000_0000; kernel loads at 0x4008_0000.
 * Place boot_info and mmap well below the kernel.
 */
#define SAFE_BOOT_INFO_ADDR   0x40005000ULL
#define SAFE_MMAP_ADDR        0x40005100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x40007000ULL - SAFE_MMAP_ADDR) / sizeof(struct boot_mmap_entry))

/* ---- Shared UEFI helpers ---- */
#include <boot/uefi_boot.h>

/* ---- Entry point ---- */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    ST = system_table;
    BS = system_table->BootServices;

    ST->ConOut->ClearScreen(ST->ConOut);
    uefi_print(L"\r\nZonix UEFI Bootloader (AArch64) v1.0\r\n\r\n");
    BS->SetWatchdogTimer(0, 0, 0, NULL);

    struct boot_info* bi = (struct boot_info*)(UINTN)SAFE_BOOT_INFO_ADDR;
    memset(bi, 0, sizeof(struct boot_info));
    bi->magic = BOOT_INFO_MAGIC;
    bi->mmap_addr = SAFE_MMAP_ADDR;

    uefi_print(L"Getting memory map...\r\n");
    EFI_STATUS status = uefi_get_memory_map(bi);
    if (EFI_ERROR(status)) {
        uefi_print(L"Memory map failed\r\n");
        return status;
    }

    uefi_print(L"Loading kernel...\r\n");
    VOID* kernel_buf = NULL;
    UINTN kernel_size = 0;
    status = uefi_load_kernel_file(image_handle, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        uefi_print(L"Kernel load failed\r\n");
        return status;
    }

    uefi_print(L"Parsing ELF kernel...\r\n");

    /*
     * EDK2 on AArch64 marks EfiConventionalMemory as non-executable.
     * Claim the kernel's load region as EfiLoaderCode before writing.
     */
    {
        EFI_PHYSICAL_ADDRESS kernel_base = 0x40080000ULL;
        UINTN kernel_pages = 256; /* 1 MB */
        status = BS->AllocatePages(AllocateAddress, EfiLoaderCode, kernel_pages, &kernel_base);
        if (EFI_ERROR(status)) {
            uefi_print(L"AllocatePages for kernel failed\r\n");
            return status;
        }
    }

    if (uefi_load_elf(kernel_buf, bi) != 0) {
        uefi_print(L"ELF parse failed\r\n");
        return EFI_LOAD_ERROR;
    }

    uefi_get_graphics_info(bi);

    const char* name = "Zonix UEFI";
    for (int i = 0; name[i] && i < 31; i++)
        bi->loader_name[i] = name[i];

    uefi_print(L"Kernel entry (phys): ");
    uefi_print_hex(bi->kernel_entry);

    uefi_print(L"Exiting Boot Services...\r\n");
    status = uefi_exit_boot_services(image_handle);
    if (EFI_ERROR(status))
        return status;

    /*
     * Jump to kernel.  AArch64 UEFI and kernel both use AAPCS64
     * (first argument in x0), so a direct function call suffices.
     */
    typedef void (*kernel_entry_fn)(struct boot_info*);
    kernel_entry_fn entry = (kernel_entry_fn)(UINTN)bi->kernel_entry;
    entry(bi);

    for (;;)
        __asm__ volatile("wfe");
    return EFI_SUCCESS;
}
