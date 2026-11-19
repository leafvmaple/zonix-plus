#pragma once

#include <base/types.h>
#include "cons/shell.h"

namespace cmd {

constexpr size_t PATH_BUF_SIZE = 128;

// The VFS owns the /mnt mount state.
bool mnt_mounted();

int build_path(const char* filename, int use_mnt, char* out, size_t out_size);

void register_fs_commands();
void register_blk_commands();
void register_sys_commands();

}  // namespace cmd
