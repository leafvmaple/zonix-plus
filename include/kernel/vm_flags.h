#pragma once

#include <base/types.h>

// Mapping requests, never hardware page-table bits. Architecture helpers encode
// these flags and inspect hardware entries through their own pte_* predicates.
inline constexpr uint32_t VM_PRESENT = 1U << 0;
inline constexpr uint32_t VM_WRITE = 1U << 1;
inline constexpr uint32_t VM_USER = 1U << 2;
inline constexpr uint32_t VM_NOCACHE = 1U << 3;
inline constexpr uint32_t VM_NOEXEC = 1U << 4;
inline constexpr uint32_t VM_USER_RW = VM_PRESENT | VM_WRITE | VM_USER | VM_NOEXEC;

inline constexpr uint32_t user_page_perm(bool write, bool executable = false) {
    return VM_USER | (write ? VM_WRITE : 0) | (executable ? 0 : VM_NOEXEC);
}
