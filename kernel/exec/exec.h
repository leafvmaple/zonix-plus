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
#include <asm/page.h>
#include "mm/pmm.h"
#include "fs/vfs.h"

namespace exec {

inline constexpr size_t MAX_BINARY_SIZE = 1024ULL * 1024ULL;  // 1 MB

/**
 * Create a new top-level page directory with kernel mappings copied
 * from boot_pgdir.
 * @return New pgdir, or nullptr on failure
 */
pde_t* create_user_pgdir();

/**
 * Map and zero-fill a user-mode stack in @p pgdir.
 * @return Top of user stack (initial stack pointer), or 0 on failure
 */
uintptr_t setup_user_stack(pde_t* pgdir);

/**
 * Execute a binary from VFS.
 *
 * Reads the file, auto-detects the format (currently ELF64; extensible),
 * builds a user address space, and forks a new user-mode process.
 *
 * @param path  Absolute or root-relative path
 * @return PID of the new process on success, negative on error
 */
int exec(const char* path);

}  // namespace exec
