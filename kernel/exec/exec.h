#pragma once

/**
 * Generic binary execution framework.
 *
 * Handles user address space creation, stack setup, process spawning,
 * and file I/O.  The actual binary parsing is delegated to format-
 * specific loaders (e.g. elf::load()).  New formats can be added by
 * implementing a loader function and registering it in exec().
 */

#include <base/types.h>
#include <asm/pg.h>
#include "mm/pmm.h"
#include "fs/fat.h"

namespace exec {

// ---- User-space memory layout (format-independent) ----
inline constexpr uintptr_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
inline constexpr size_t USER_STACK_SIZE = 4 * PG_SIZE;  // 16 KB
inline constexpr size_t MAX_BINARY_SIZE = 1024 * 1024;  // 1 MB

/**
 * Create a new PML4 page directory with kernel mappings (entries 256-511)
 * copied from boot_pgdir.
 * @return New PML4, or nullptr on failure
 */
pde_t* create_user_pgdir();

/**
 * Map and zero-fill a user-mode stack in @p pgdir.
 * @return Top of user stack (initial RSP), or 0 on failure
 */
uintptr_t setup_user_stack(pde_t* pgdir);

/**
 * Execute a binary from a FAT filesystem.
 *
 * Reads the file, auto-detects the format (currently ELF64; extensible),
 * builds a user address space, and forks a new user-mode process.
 *
 * @param path  Filename to look up in the FAT root directory
 * @param fat   Mounted FAT filesystem to read from
 * @return PID of the new process on success, negative on error
 */
int exec(const char* path, FatInfo* fat);

}  // namespace exec
