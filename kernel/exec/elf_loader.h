#pragma once

/**
 * ELF64 binary loader — pure parsing and segment loading.
 *
 * This module knows nothing about process creation, scheduling, or
 * filesystems.  It only validates ELF headers and copies PT_LOAD
 * segments into an already-prepared page directory.
 *
 * The generic execution framework lives in exec.h / exec.cpp.
 */

#include <base/elf.h>
#include <base/types.h>
#include <asm/pg.h>
#include "mm/pmm.h"

namespace elf {

// Maximum ELF file size we are willing to load
inline constexpr size_t ELF_MAX_SIZE = 1024 * 1024;  // 1 MB

/**
 * Check whether a buffer starts with the ELF magic number.
 * Suitable for quick format detection before committing to full
 * validation.
 *
 * @param data  Pointer to file data (at least 4 bytes)
 * @param size  Size of available data
 * @return true if the first 4 bytes match "\x7fELF"
 */
bool is_elf(const uint8_t* data, size_t size);

/**
 * Validate an ELF64 executable header.
 * Checks magic, class (64-bit), type (ET_EXEC), machine (x86-64),
 * and that program headers fall within file bounds.
 *
 * @return 0 on success, -1 on failure (diagnostic printed)
 */
int validate(const elfhdr* eh, size_t file_size);

/**
 * Load all PT_LOAD segments into a page directory.
 *
 * Allocates physical pages via the PMM, maps them into @p pgdir with
 * appropriate permissions, and copies segment data.  BSS regions are
 * zero-filled.
 *
 * @param data   Raw ELF file data in kernel memory
 * @param size   Size of the ELF file in bytes
 * @param pgdir  Target PML4 (must already contain kernel mappings)
 * @return Entry-point virtual address, or 0 on failure
 */
uintptr_t load(const uint8_t* data, size_t size, pde_t* pgdir);

}  // namespace elf
