#include "fd.h"
#include "fs/vfs.h"

namespace fd {

void Table::init() {
    for (auto& entry : entries_) {
        entry.reset();
    }
}

int Table::alloc(vfs::File* file) {
    if (!file) {
        return -1;
    }

    for (auto& entry : entries_) {
        if (!entry.used) {
            entry.set(file, 0, true);
            return &entry - entries_;
        }
    }

    return -1;
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

int Table::close(int fd) {
    Entry* entry = get(fd);
    if (!entry) {
        return -1;
    }

    vfs::close(entry->file);
    entry->reset();

    return 0;
}

void Table::close_all() {
    for (auto& entry : entries_) {
        if (entry.used && entry.file) {
            vfs::close(entry.file);
        }
        entry.reset();
    }
}

int Table::fork_from(const Table& parent, ForkPolicy policy) {
    switch (policy) {
        case ForkPolicy::Reset: init(); return 0;
        case ForkPolicy::Share:
            static_cast<void>(parent);
            // Shared file descriptions need refcounted open-file objects.
            return -1;
        default: return -1;
    }
}

}  // namespace fd