#include "syscall.h"
#include "fs/channel.h"
#include "fs/fd.h"
#include "sched/sched.h"
#include <abi/channel.h>

namespace {

constexpr long ACCEPTED_FLAGS = O_CLOEXEC | O_NONBLOCK;

long install(Task* task, Result<vfs::FileHandle> file, long flags) {
    if (!file.ok())
        return static_cast<long>(file.error());
    auto fd = task->files().alloc(file.release_value(), (flags & O_CLOEXEC) != 0);
    return fd.ok() ? fd.value() : static_cast<long>(fd.error());
}

}  // namespace

long syscall::channel_open(Task* task, uintptr_t name, long flags, bool listening) {
    if (!task || (flags & ~ACCEPTED_FLAGS) != 0)
        return static_cast<long>(Error::Invalid);
    if (!task->files().has_space())
        return static_cast<long>(Error::Full);
    char text[CHANNEL_NAME_MAX + 1]{};
    Error error = copy_string(task, name, text, sizeof(text));
    if (error != Error::None)
        return static_cast<long>(error);
    bool nonblocking = (flags & O_NONBLOCK) != 0;
    return install(task, listening ? channel::listen(text, nonblocking) : channel::connect(text, nonblocking), flags);
}

long syscall::channel_accept(Task* task, long fd, long flags) {
    if (!task || fd < 0 || fd >= fd::MAX_FD || (flags & ~ACCEPTED_FLAGS) != 0)
        return static_cast<long>(Error::Invalid);
    auto* entry = task->files().get(static_cast<int>(fd));
    channel::Listener* listener = entry ? entry->file()->channel_listener() : nullptr;
    if (!listener)
        return static_cast<long>(Error::Invalid);
    if (!task->files().has_space())
        return static_cast<long>(Error::Full);
    return install(task, channel::accept(*listener, (flags & O_NONBLOCK) != 0), flags);
}
