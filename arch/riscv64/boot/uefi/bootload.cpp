#include "kernel/bootinfo.h"
#include <uefi/uefi.h>
#include <boot/uefi_boot.h>
#include <asm/board.h>

#define SAFE_MMAP_MAX_ENTRIES ((BOARD_KERNEL_PHYS - BOARD_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = UEFI_STR(L"\r\nZonix UEFI Bootloader (RISC-V 64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI RISC-V",
        .kernel_virt_base = 0xFFFFFFC000000000ULL,
        .boot_info_addr = BOARD_BOOT_INFO_ADDR,
        .mmap_addr = BOARD_MMAP_ADDR,
        .mmap_max_entries = SAFE_MMAP_MAX_ENTRIES,
        .mem_lower = 0,
        .mem_upper_min = 0,
        .kernel_alloc_base = BOARD_KERNEL_PHYS,
        .kernel_alloc_pages = 512, /* 2 MB */
    };

    BootInfo* bi = nullptr;
    EFI_STATUS status = uefi_boot_setup(image_handle, system_table, cfg, &bi);
    if (EFI_ERROR(status)) {
        return status;
    }

    __asm__ volatile("csrw satp, zero\n"
                     "sfence.vma zero, zero\n" ::
                         : "memory");

    /*
     * Jump to kernel physical entry.
     * Calling convention: a0 = hart_id, a1 = &boot_info (phys).
     * Read the actual hart ID from the tp register (set by firmware).
     */
    unsigned long hart_id;
    __asm__ volatile("mv %0, tp" : "=r"(hart_id));

    using kernel_entry_fn = void (*)(unsigned long hart_id, BootInfo* bi);
    auto entry = reinterpret_cast<kernel_entry_fn>(bi->kernel_entry);
    entry(hart_id, bi);

    for (;;) {
        __asm__ volatile("wfi");
    }
    return EFI_SUCCESS;
}
