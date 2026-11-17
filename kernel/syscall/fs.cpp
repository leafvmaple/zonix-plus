#include "syscall.h"
#include "exec/exec.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include <abi/process.h>

static_assert(sizeof(DescriptorStat) == 32);

namespace {
long path_operation(Task* task, const char* user, Error (*operation)(const char*)) {
    char path[exec::MAX_PATH]{};
    Error error = syscall::copy_path(task, user, path);
    return static_cast<long>(error == Error::None ? operation(path) : error);
}
}  // namespace

long syscall::mkdir(Task* task, const char* path) {
    return path_operation(task, path, vfs::mkdir);
}
long syscall::unlink(Task* task, const char* path) {
    return path_operation(task, path, vfs::unlink);
}
long syscall::rmdir(Task* task, const char* path) {
    return path_operation(task, path, vfs::rmdir);
}

long syscall::fstat(Task* task, int fd, uintptr_t output) {
    if (!task || !vmm::user_range_valid(task->memory(), output, sizeof(DescriptorStat), true))
        return static_cast<long>(Error::Invalid);
    auto* entry = task->files().get(fd);
    if (!entry)
        return static_cast<long>(Error::Invalid);
    ActivityGuard activity;
    Error error = entry->file()->begin_io(activity);
    if (error != Error::None)
        return static_cast<long>(error);
    vfs::Stat st{};
    error = entry->file()->stat(&st);
    if (error != Error::None)
        return static_cast<long>(error);
    DescriptorStat result{
        static_cast<unsigned int>(st.type), st.size, st.attrs, 0, static_cast<long>(entry->file()->device_id()),
        static_cast<long>(st.file_id)};
    return static_cast<long>(vmm::copy_to_user(task->memory(), output, &result, sizeof(result)));
}

long syscall::ftruncate(Task* task, int fd, long length) {
    if (!task || length < 0)
        return static_cast<long>(Error::Invalid);
    auto* entry = task->files().get(fd);
    if (!entry)
        return static_cast<long>(Error::Invalid);
    ActivityGuard activity;
    Error error = entry->file()->begin_io(activity);
    if (error != Error::None)
        return static_cast<long>(error);
    error = entry->lock_interruptible();
    if (error != Error::None)
        return static_cast<long>(error);
    sys::lock_guard<fd::OpenFile> lock(*entry, sys::adopt_lock);
    return static_cast<long>(vfs::truncate(entry->file(), static_cast<size_t>(length)));
}
