#pragma once

#include <base/types.h>
#include "cons/shell.h"

namespace cmd {

constexpr size_t PATH_BUF_SIZE = 128;

// Mount state shared across command files.
const char* mnt_device();
int mnt_mounted();
void set_mnt_device(const char* name);
void set_mnt_mounted(int val);

int build_path(const char* filename, int use_mnt, char* out, size_t out_size);

void register_fs_commands();
void register_blk_commands();
void register_sys_commands();

}  // namespace cmd
