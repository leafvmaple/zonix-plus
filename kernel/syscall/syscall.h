#pragma once

#include <base/types.h>
#include "exec/exec.h"
#include "lib/result.h"

struct Task;
namespace vfs {
class File;
}

namespace syscall {
Error copy_string(Task* task, uintptr_t user, char* out, size_t capacity);
// Resolves a user path against the task's working directory. The buffer's
// size is part of the type so a caller cannot pass a shorter one.
Error copy_path(Task* task, const char* user, char (&path)[exec::MAX_PATH]);
long chdir(Task* task, const char* path);
long getcwd(Task* task, uintptr_t output, size_t capacity);
long brk(Task* task, uintptr_t end);
long spawn(Task* task, const char* path, long argc, uintptr_t argv, bool foreground = false);
long wait(Task* task, long pid, uintptr_t status);
long process_next(Task* task, long after_pid, uintptr_t output);
long set_priority(Task* task, long pid, long priority);
long mkdir(Task* task, const char* path);
long rename(Task* task, const char* path, uintptr_t new_name);
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
long getrandom(Task* task, uintptr_t output, long size, uintptr_t flags);
long ioctl(Task* task, long fd, long request, uintptr_t argument);
long display_control(Task* task, vfs::File* file, unsigned int request, uintptr_t argument);
long pipe2(Task* task, uintptr_t output, long flags);
long openpty(Task* task, uintptr_t output, uintptr_t input);
long dup2(Task* task, long old_fd, long new_fd);
long fcntl(Task* task, long fd, long command, long argument);
// Registers (listening) or connects to a named channel and returns its descriptor.
long channel_open(Task* task, uintptr_t name, long flags, bool listening);
long channel_accept(Task* task, long fd, long flags);
// False means an unknown syscall number; result preserves the existing ABI.
bool dispatch(Task* task, long nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, long& result);
}  // namespace syscall
