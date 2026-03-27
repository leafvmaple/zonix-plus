#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

/*
 * Safe fixed physical addresses for boot_info (below kernel at 0x100000).
 * Must not conflict with BIOS IVT (0-0x1000), BDA, or VGA memory.
 *   0x5000 - 0x50FF  : struct boot_info
 *   0x5100 - 0x6FFF  : mmap entries
 */
#define SAFE_BOOT_INFO_ADDR   0x5000ULL
#define SAFE_MMAP_ADDR        0x5100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x7000ULL - SAFE_MMAP_ADDR) / sizeof(boot_mmap_entry))

#define PLATFORM_MEM_LOWER     640
#define PLATFORM_MEM_UPPER_MIN 0x100000

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    EFI_SYSTEM_TABLE* st = system_table;
    EFI_BOOT_SERVICES* bs = system_table->BootServices;

    st->ConOut->ClearScreen(st->ConOut);
    uefi_print(st, UEFI_STR(L"\r\nZonix UEFI Bootloader (x86_64) v1.0\r\n\r\n"));
    bs->SetWatchdogTimer(0, 0, 0, 0);

    boot_info* bi = (boot_info*)(UINTN)SAFE_BOOT_INFO_ADDR;
    memset(bi, 0, sizeof(boot_info));
    bi->magic = BOOT_INFO_MAGIC;
    bi->mmap_addr = SAFE_MMAP_ADDR;

    uefi_print(st, UEFI_STR(L"Getting memory map...\r\n"));
    EFI_STATUS status =
        uefi_get_memory_map(bs, bi, SAFE_MMAP_ADDR, SAFE_MMAP_MAX_ENTRIES, PLATFORM_MEM_LOWER, PLATFORM_MEM_UPPER_MIN);
    if (EFI_ERROR(status)) {
        return status;
    }

    uefi_print(st, UEFI_STR(L"Loading kernel...\r\n"));
    VOID* kernel_buf = 0;
    UINTN kernel_size = 0;
    status = uefi_load_kernel_file(bs, image_handle, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        return status;
    }

    uefi_print(st, UEFI_STR(L"Parsing ELF kernel...\r\n"));
    if (uefi_load_elf(kernel_buf, bi, KERNEL_VIRT_BASE) != 0) {
        return EFI_LOAD_ERROR;
    }

    uefi_get_graphics_info(bs, bi);

    const char* name = "Zonix UEFI";
    for (int i = 0; name[i] && i < 31; i++)
        bi->loader_name[i] = name[i];

    uefi_print(st, UEFI_STR(L"Kernel entry (phys): "));
    uefi_print_hex(st, bi->kernel_entry);

    uefi_print(st, UEFI_STR(L"Exiting Boot Services...\r\n"));
    status = uefi_exit_boot_services(bs, image_handle);
    if (EFI_ERROR(status)) {
        return status;
    }

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
