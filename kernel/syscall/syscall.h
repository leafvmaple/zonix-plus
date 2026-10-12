#pragma once

#include <base/types.h>

struct Task;

namespace syscall {
long open(Task* task, const char* path, int flags, int mode);
long read(Task* task, int fd, void* buf, size_t count);
long write(Task* task, int fd, const char* buf, size_t count);
long close(Task* task, int fd);
// False means an unknown syscall number; result preserves the existing ABI.
bool dispatch(Task* task, long nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, long& result);
}  // namespace syscall
