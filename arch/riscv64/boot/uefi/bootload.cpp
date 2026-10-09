#include "kernel/bootinfo.h"
#include <uefi/uefi.h>
#include <boot/uefi_boot.h>
#include <asm/board.h>

#define SAFE_MMAP_MAX_ENTRIES ((BOARD_KERNEL_PHYS - BOARD_MMAP_ADDR) / sizeof(BootMemEntry))

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    const UefiBootConfig cfg = {
        .banner = uefi_string(L"\r\nZonix UEFI Bootloader (RISC-V 64) v1.0\r\n\r\n"),
        .loader_name = "Zonix UEFI RISC-V",
        .kernel_base_va = 0xFFFFFFC000000000ULL,
        .boot_info_pa = BOARD_BOOT_INFO_ADDR,
        .memory_map_pa = BOARD_MMAP_ADDR,
        .memory_map_capacity = SAFE_MMAP_MAX_ENTRIES,
        .lower_memory_kib = 0,
        .upper_memory_start_pa = 0,
        .kernel_alloc_pa = BOARD_KERNEL_PHYS,
        .kernel_page_count = 512, /* 2 MB */
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

    using HartKernelEntry = void (*)(unsigned long hart_id, BootInfo* bi);
    auto entry = reinterpret_cast<HartKernelEntry>(bi->kernel_entry);
    entry(hart_id, bi);

    for (;;) {
        __asm__ volatile("wfi");
    }
    return EFI_SUCCESS;
}
