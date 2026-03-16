/**
 * @file bootload.c
 * @brief Zonix UEFI bootloader — x86_64 platform layer.
 *
 * Common UEFI boot logic lives in <boot/uefi_boot.h>.
 * This file provides only x86_64-specific constants and the kernel jump.
 */

#include <uefi/uefi.h>

/* Global UEFI state (referenced by uefi_boot.h helpers) */
static EFI_SYSTEM_TABLE* ST;
static EFI_BOOT_SERVICES* BS;

/* ---- Platform configuration ---- */

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

/*
 * Safe fixed physical addresses for boot_info (below kernel at 0x100000).
 * Must not conflict with BIOS IVT (0-0x1000), BDA, or VGA memory.
 *   0x5000 - 0x50FF  : struct boot_info
 *   0x5100 - 0x6FFF  : mmap entries
 */
#define SAFE_BOOT_INFO_ADDR   0x5000ULL
#define SAFE_MMAP_ADDR        0x5100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x7000ULL - SAFE_MMAP_ADDR) / sizeof(struct boot_mmap_entry))

#define PLATFORM_MEM_LOWER     640
#define PLATFORM_MEM_UPPER_MIN 0x100000

/* ---- Shared UEFI helpers ---- */
#include <boot/uefi_boot.h>

/* ---- Entry point ---- */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    ST = system_table;
    BS = system_table->BootServices;

    ST->ConOut->ClearScreen(ST->ConOut);
    uefi_print(L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n");
    BS->SetWatchdogTimer(0, 0, 0, NULL);

    struct boot_info* bi = (struct boot_info*)(UINTN)SAFE_BOOT_INFO_ADDR;
    memset(bi, 0, sizeof(struct boot_info));
    bi->magic = BOOT_INFO_MAGIC;
    bi->mmap_addr = SAFE_MMAP_ADDR;

    uefi_print(L"Getting memory map...\r\n");
    EFI_STATUS status = uefi_get_memory_map(bi);
    if (EFI_ERROR(status))
        return status;

    uefi_print(L"Loading kernel...\r\n");
    VOID* kernel_buf = NULL;
    UINTN kernel_size = 0;
    status = uefi_load_kernel_file(image_handle, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status))
        return status;

    uefi_print(L"Parsing ELF kernel...\r\n");
    if (uefi_load_elf(kernel_buf, bi) != 0)
        return EFI_LOAD_ERROR;

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
     * Jump to kernel entry.
     * This UEFI binary uses the Microsoft x64 ABI (first arg in %rcx)
     * but the kernel expects System V ABI (first arg in %rdi).
     * Use inline assembly to place boot_info in %rdi explicitly.
     */
    UINT64 entry_addr = (UINT64)(UINTN)bi->kernel_entry;
    UINT64 info_addr = (UINT64)(UINTN)bi;
    __asm__ volatile("movq %0, %%rdi\n\t"
                     "jmp *%1\n\t"
                     :
                     : "r"(info_addr), "r"(entry_addr)
                     : "rdi", "memory");

    return EFI_SUCCESS;
}
