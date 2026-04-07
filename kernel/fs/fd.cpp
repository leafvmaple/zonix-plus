#include "fd.h"
#include "fs/vfs.h"

namespace fd {

void Table::init() {
    for (auto& entry : entries_) {
        entry.reset();
    }
}

Result<int> Table::alloc(vfs::File* file) {
    ENSURE(file, Error::Invalid);

    for (auto& entry : entries_) {
        if (!entry.used) {
            entry.set(file, 0, true);
            return static_cast<int>(&entry - entries_);
        }
    }

    return Error::Full;
}

Entry* Table::get(int fd) {
    if (fd < 0 || fd >= MAX_FD) {
        return nullptr;
    }

    if (!entries_[fd].used || !entries_[fd].file) {
        return nullptr;
    }

    return &entries_[fd];
}

Error Table::close(int fd) {
    Entry* entry = get(fd);
    if (!entry) {
        return Error::Invalid;
    }

    vfs::close(entry->file);
    entry->reset();

    return Error::None;
}

void Table::close_all() {
    for (auto& entry : entries_) {
        if (entry.used && entry.file) {
            vfs::close(entry.file);
        }
        entry.reset();
    }
}

Error Table::fork_from(const Table& parent, ForkPolicy policy) {
    switch (policy) {
        case ForkPolicy::Reset: init(); return Error::None;
        case ForkPolicy::Share:
            static_cast<void>(parent);
            // Shared file descriptions need refcounted open-file objects.
            return Error::NotSupported;
        default: return Error::Invalid;
    }
}

}  // namespace fd