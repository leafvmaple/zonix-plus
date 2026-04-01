#include "kernel/bootinfo.h"
#include <uefi/uefi.h>
#include <boot/uefi_boot.h>

#define KERNEL_VIRT_BASE 0xFFFFFFC000000000ULL
#define KERNEL_PHYS      0x80200000UL

#define SAFE_BOOT_INFO_ADDR   0x80180000UL
#define SAFE_MMAP_ADDR        0x80180100UL
#define SAFE_MMAP_MAX_ENTRIES ((0x80200000UL - SAFE_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    EFI_SYSTEM_TABLE* st = system_table;
    EFI_BOOT_SERVICES* bs = system_table->BootServices;

    st->ConOut->ClearScreen(st->ConOut);
    uefi_print(st, UEFI_STR(L"\r\nZonix UEFI Bootloader (RISC-V 64) v1.0\r\n\r\n"));
    bs->SetWatchdogTimer(0, 0, 0, nullptr);

    auto* bi = reinterpret_cast<BootInfo*>(SAFE_BOOT_INFO_ADDR);
    memset(bi, 0, sizeof(BootInfo));
    bi->magic = BOOT_INFO_MAGIC;
    bi->mmap_addr = SAFE_MMAP_ADDR;

    uefi_print(st, UEFI_STR(L"Getting memory map...\r\n"));
    EFI_STATUS status = uefi_get_memory_map(bs, bi, SAFE_MMAP_ADDR, SAFE_MMAP_MAX_ENTRIES, 0, 0);
    if (EFI_ERROR(status)) {
        uefi_print(st, UEFI_STR(L"Memory map failed\r\n"));
        return status;
    }


    uefi_print(st, UEFI_STR(L"Loading kernel ELF...\r\n"));
    void* kernel_buf = nullptr;
    uintptr_t kernel_size = 0;
    status = uefi_load_kernel_file(bs, image_handle, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        uefi_print(st, UEFI_STR(L"Kernel load failed\r\n"));
        return status;
    }

    /* Allocate the physical kernel region as EfiLoaderCode so that
     * EDK2 does not reclaim it after ExitBootServices */
    {
        EFI_PHYSICAL_ADDRESS kbase = KERNEL_PHYS;
        uintptr_t kpages = 512; /* 2 MB */
        status = bs->AllocatePages(AllocateAddress, EfiLoaderCode, kpages, &kbase);
        if (EFI_ERROR(status)) {
            uefi_print(st, UEFI_STR(L"AllocatePages for kernel failed\r\n"));
            return status;
        }
    }

    uefi_print(st, UEFI_STR(L"Parsing ELF kernel...\r\n"));
    if (uefi_load_elf(kernel_buf, bi, KERNEL_VIRT_BASE) != 0) {
        uefi_print(st, UEFI_STR(L"ELF parse/load failed\r\n"));
        return EFI_LOAD_ERROR;
    }

    uefi_get_graphics_info(bs, bi);

    const char* lname = "Zonix UEFI RISC-V";
    for (int i = 0; lname[i] && i < 31; i++) {
        bi->loader_name[i] = lname[i];
    }

    uefi_print(st, UEFI_STR(L"Kernel entry (phys): "));
    uefi_print_hex(st, bi->kernel_entry);
    uefi_print(st, UEFI_STR(L"\r\n"));

    /* Exit boot services — after this UEFI is gone */
    uefi_print(st, UEFI_STR(L"Exiting Boot Services...\r\n"));
    status = uefi_exit_boot_services(bs, image_handle);
    if (EFI_ERROR(status)) {
        return status;
    }

    __asm__ volatile("csrw satp, zero\n"
                     "sfence.vma zero, zero\n" ::
                         : "memory");

    /*
     * Jump to kernel physical entry.
     * Calling convention: a0 = hart_id (0), a1 = &boot_info (phys).
     */
    using kernel_entry_fn = void (*)(unsigned long hart_id, BootInfo* bi);
    auto entry = reinterpret_cast<kernel_entry_fn>(bi->kernel_entry);
    entry(0, bi);

    for (;;) {
        __asm__ volatile("wfi");
    }
    return EFI_SUCCESS;
}
