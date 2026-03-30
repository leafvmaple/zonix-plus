#pragma once

#include <uefi/uefi.h>
#include <kernel/bootinfo.h>
#include <base/elf.h>

void* uefi_memcpy(void* dst, const void* src, uintptr_t n);
void* uefi_memset(void* dst, int c, uintptr_t n);
void uefi_print(EFI_SYSTEM_TABLE* st, const wchar_t* str);
void uefi_print_hex(EFI_SYSTEM_TABLE* st, uint64_t val);
int uefi_load_elf(void* elf_buffer, struct BootInfo* bi, uint64_t kernel_virt_base);
EFI_STATUS uefi_get_memory_map(EFI_BOOT_SERVICES* bs, struct BootInfo* bi, uint64_t mmap_addr,
                               uintptr_t mmap_max_entries, uint32_t mem_lower, uint64_t mem_upper_min);
EFI_STATUS uefi_load_kernel_file(EFI_BOOT_SERVICES* bs, EFI_HANDLE image_handle, void** buf, uintptr_t* size);
void uefi_get_graphics_info(EFI_BOOT_SERVICES* bs, struct BootInfo* bi);
EFI_STATUS uefi_exit_boot_services(EFI_BOOT_SERVICES* bs, EFI_HANDLE image_handle);
