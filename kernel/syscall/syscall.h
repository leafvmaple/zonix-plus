#pragma once

#include <base/types.h>
#include "lib/result.h"

struct Task;

namespace syscall {
Error copy_string(Task* task, uintptr_t user, char* out, size_t capacity);
Error copy_path(Task* task, const char* user, char* path);
long chdir(Task* task, const char* path);
long getcwd(Task* task, uintptr_t output, size_t capacity);
long spawn(Task* task, const char* path, long argc, uintptr_t argv, bool foreground = false);
long wait(Task* task, long pid, uintptr_t status);
long process_next(Task* task, long after_pid, uintptr_t output);
long mkdir(Task* task, const char* path);
long unlink(Task* task, const char* path);
long rmdir(Task* task, const char* path);
long fstat(Task* task, int fd, uintptr_t output);
long ftruncate(Task* task, int fd, long length);
long stat(Task* task, const char* path, uintptr_t output);
long readdir(Task* task, const char* path, long index, uintptr_t output);
long open(Task* task, const char* path, int flags, int mode);
long read(Task* task, int fd, void* buf, size_t count);
long write(Task* task, int fd, const char* buf, size_t count);
long close(Task* task, int fd);
// False means an unknown syscall number; result preserves the existing ABI.
bool dispatch(Task* task, long nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, long& result);
}  // namespace syscall
