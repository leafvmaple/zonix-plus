#pragma once

#include <base/types.h>

// Kernel-owned, physically contiguous memory. Small blocks are aligned to their
// size class (at least 16 bytes); blocks larger than 1024 bytes are page-aligned.
// Zero bytes and allocation failure return nullptr. Free only the returned base.
[[nodiscard]] void* kmalloc(size_t byte_count);
void kfree(void* ptr);
