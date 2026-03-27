#pragma once

#include <base/elf.h>
#include <base/types.h>
#include <asm/page.h>
#include "mm/pmm.h"

namespace elf {

inline constexpr size_t ELF_MAX_SIZE = 1024ULL * 1024ULL;  // 1 MB

bool is_elf(const uint8_t* data, size_t size);
int validate(const ElfHdr* eh, size_t file_size);
uintptr_t load(const uint8_t* data, size_t size, pde_t* pgdir);

}  // namespace elf
