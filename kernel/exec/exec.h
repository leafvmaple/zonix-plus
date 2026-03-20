#pragma once

#include <base/types.h>
#include <asm/page.h>
#include "mm/pmm.h"
#include "fs/vfs.h"

namespace exec {

inline constexpr size_t MAX_BINARY_SIZE = 1024ULL * 1024ULL;  // 1 MB

pde_t* create_user_pgdir();
uintptr_t setup_user_stack(pde_t* pgdir);

int exec(const char* path);

}  // namespace exec
