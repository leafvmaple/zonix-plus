#include "syscall.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "lib/kernel_buffer.h"
#include <asm/page.h>

namespace {
constexpr size_t SYSCALL_PATH_MAX = 128;
enum class FileIo { Read, Write };


long sys_file_io(Task* cur, int fd, uintptr_t user_buf, size_t count, FileIo operation) {
    const bool to_user = operation == FileIo::Read;
    if (!cur) {
        return -1;
    }
    fd::OpenFile* entry = cur->files().get(fd);
    if (!entry || (to_user ? !entry->file()->can_read() : !entry->file()->can_write())) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    ActivityGuard activity;
    if (entry->file()->begin_io(activity) != Error::None) {
        return -1;
    }
    if (entry->lock_interruptible() != Error::None) {
        return -1;
    }
    sys::lock_guard<fd::OpenFile> file_lock(*entry, sys::adopt_lock);
    if (!vmm::user_range_valid(cur->memory(), user_buf, count, to_user)) {
        return -1;
    }

    const bool append = !to_user && entry->file()->is_append();
    if (append && count > 0x7FFFFFFFU) {
        return -1;
    }
    // One append syscall must reach the backend as one serialized EOF selection/write.
    auto allocation = KernelBuffer::alloc(append ? count : PG_SIZE);
    if (!allocation.ok()) {
        return -1;
    }
    auto storage = allocation.release_value();
    uint8_t* buf = storage.data();
    size_t done = 0;
    bool failed = false;
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > storage.size()) {
            chunk = storage.size();
        }
        if (!to_user && vmm::copy_from_user(cur->memory(), buf, user_buf + done, chunk) != Error::None) {
            failed = true;
            break;
        }
        size_t next_offset = entry->offset();
        auto bytes_r = to_user ? vfs::read(entry->file(), buf, chunk, entry->offset())
                               : vfs::write(entry->file(), buf, chunk, entry->offset(), &next_offset);
        if (!bytes_r.ok() || bytes_r.value() < 0 || static_cast<size_t>(bytes_r.value()) > chunk) {
            failed = true;
            break;
        }
        size_t bytes = static_cast<size_t>(bytes_r.value());
        if (to_user && vmm::copy_to_user(cur->memory(), user_buf + done, buf, bytes) != Error::None) {
            failed = true;
            break;
        }
        entry->set_offset(to_user ? entry->offset() + bytes : next_offset);
        done += bytes;
        if (bytes < chunk) {
            break;
        }
    }
    return failed && done == 0 ? -1 : static_cast<long>(done);
}

}  // namespace

long syscall::open(Task* cur, const char* user_path, int flags, int mode) {
    if (!cur || mode != 0 || !cur->files().has_space()) {
        return -1;
    }

    char path[SYSCALL_PATH_MAX]{};
    if (copy_path(cur, user_path, path) != Error::None) {
        return -1;
    }

    auto result = cur->files().open(path, flags);
    return result.ok() ? result.value() : -1;
}

long syscall::dup2(Task* task, long old_fd, long new_fd) {
    if (!task || old_fd < 0 || old_fd >= fd::MAX_FD || new_fd < 0 || new_fd >= fd::MAX_FD)
        return static_cast<long>(Error::Invalid);
    Error error = task->files().duplicate_to(static_cast<int>(old_fd), static_cast<int>(new_fd));
    return error == Error::None ? new_fd : static_cast<long>(error);
}

long syscall::fcntl(Task* task, long fd, long command, long argument) {
    if (!task || fd < 0 || fd >= fd::MAX_FD || !task->files().get(static_cast<int>(fd)))
        return static_cast<long>(Error::Invalid);
    if (command != F_DUPFD_CLOEXEC)
        return static_cast<long>(Error::NotSupported);
    if (argument < 0 || argument >= fd::MAX_FD)
        return static_cast<long>(Error::Invalid);
    auto result = task->files().duplicate_cloexec(static_cast<int>(fd), static_cast<int>(argument));
    return result.ok() ? result.value() : static_cast<long>(result.error());
}

long syscall::read(Task* cur, int fd, void* user_buf, size_t count) {
    if (!cur) {
        return -1;
    }
    return sys_file_io(cur, fd, reinterpret_cast<uintptr_t>(user_buf), count, FileIo::Read);
}

long syscall::close(Task* cur, int fd) {
    if (!cur) {
        return -1;
    }
    return cur->files().close(fd) == Error::None ? 0 : -1;
}

long syscall::write(Task* cur, int fd, const char* user_buf, size_t count) {
    return sys_file_io(cur, fd, reinterpret_cast<uintptr_t>(user_buf), count, FileIo::Write);
}
