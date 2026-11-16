#include "syscall.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "lib/kernel_buffer.h"
#include <asm/page.h>

namespace {
constexpr size_t SYSCALL_PATH_MAX = 128;
enum class FileIo { Read, Write };

int copy_user_cstr(Task* cur, const char* user, char* out, size_t out_size) {
    if (!cur || !user || !out || out_size == 0) {
        return -1;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(user);
    if (base >= USER_SPACE_TOP) {
        return -1;
    }

    for (size_t i = 0; i < out_size; i++) {
        uintptr_t addr = base + i;
        if (addr >= USER_SPACE_TOP) {
            return -1;
        }

        char ch;
        if (vmm::copy_from_user(cur->memory(), &ch, addr, 1) != Error::None) {
            return -1;
        }
        out[i] = ch;
        if (ch == '\0') {
            return 0;
        }
    }

    out[out_size - 1] = '\0';
    return -1;
}

long sys_file_io(Task* cur, int fd, uintptr_t user_buf, size_t count, FileIo operation) {
    const bool to_user = operation == FileIo::Read;
    if (!cur) {
        return -1;
    }
    fd::Entry* entry = cur->files().get(fd);
    if (!entry || (to_user ? !entry->file->can_read() : !entry->file->can_write())) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (!vmm::user_range_valid(cur->memory(), user_buf, count, to_user)) {
        return -1;
    }

    const bool append = !to_user && entry->file->is_append();
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
        size_t next_offset = entry->offset;
        auto bytes_r = to_user ? vfs::read(entry->file.get(), buf, chunk, entry->offset)
                               : vfs::write(entry->file.get(), buf, chunk, entry->offset, &next_offset);
        if (!bytes_r.ok() || bytes_r.value() < 0 || static_cast<size_t>(bytes_r.value()) > chunk) {
            failed = true;
            break;
        }
        size_t bytes = static_cast<size_t>(bytes_r.value());
        if (to_user && vmm::copy_to_user(cur->memory(), user_buf + done, buf, bytes) != Error::None) {
            failed = true;
            break;
        }
        entry->offset = to_user ? entry->offset + bytes : next_offset;
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
    if (copy_user_cstr(cur, user_path, path, sizeof(path)) != 0) {
        return -1;
    }

    auto file = vfs::open(path, flags);
    if (!file.ok()) {
        return -1;
    }

    auto fd_r = cur->files().alloc(file.release_value());
    if (!fd_r.ok()) {
        return -1;
    }

    return fd_r.value();
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
