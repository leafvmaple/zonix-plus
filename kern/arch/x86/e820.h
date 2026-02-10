#pragma once

#include <base/types.h>
#include <arch/x86/segments.h>
#include <kernel/bootinfo.h>

#define E820_MAX 20  // number of entries in E820MAP
#define E820_RAM BOOT_MEM_AVAILABLE

// Declared in head.S — kernel's own copy of boot_info
extern struct boot_info __kernel_boot_info;

template<typename F>
void traverse_e820_map(F&& callback) {
    struct boot_info *bi = &__kernel_boot_info;
    // mmap_addr was set by the bootloader using physical addresses;
    // add KERNEL_BASE to access it from the higher-half kernel.
    struct boot_mmap_entry *entries =
        reinterpret_cast<struct boot_mmap_entry*>(bi->mmap_addr + KERNEL_BASE);
    uint32_t count = bi->mmap_length;
    for (uint32_t i = 0; i < count; i++) {
        callback(entries[i].addr, entries[i].len, entries[i].type);
    }
}
