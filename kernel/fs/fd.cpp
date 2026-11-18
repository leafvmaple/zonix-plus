#include "fd.h"
#include "drivers/intr.h"

namespace fd {

OpenFileRef OpenFile::share() {
    intr::Guard guard;
    ++references_;
    return OpenFileRef(this);
}

void ReleaseOpenFile::operator()(OpenFile* file) const noexcept {
    bool last;
    {
        intr::Guard guard;
        assert(file->references_ != 0);
        last = --file->references_ == 0;
    }
    if (last)
        delete file;
}

void Table::init() {
    close_all();
}

Result<int> Table::install(OpenFileRef file, bool close_on_exec, int minimum) {
    ENSURE(file && file->file_, Error::Invalid);
    for (int i = minimum; i < MAX_FD; ++i) {
        if (!entries_[i].file) {
            entries_[i] = Entry{sys::move(file), close_on_exec};
            return i;
        }
    }
    return Error::Full;
}

Result<int> Table::alloc(vfs::FileHandle file, bool close_on_exec) {
    ENSURE(file, Error::Invalid);
    ENSURE(has_space(), Error::Full);
    OpenFileRef description(new (sys::nothrow) OpenFile());
    ENSURE(description, Error::NoMem);
    description->file_ = sys::move(file);
    return install(sys::move(description), close_on_exec);
}

Result<int> Table::open(const char* path, int flags) {
    ENSURE(has_space(), Error::Full);
    OpenFileRef description(new (sys::nothrow) OpenFile());
    ENSURE(description, Error::NoMem);
    description->file_ = TRY(vfs::open(path, flags & ~O_CLOEXEC));
    return install(sys::move(description), (flags & O_CLOEXEC) != 0);
}

bool Table::has_space() const {
    return size() < MAX_FD;
}
size_t Table::size() const {
    size_t count{};
    for (const auto& file : entries_)
        count += static_cast<bool>(file.file);
    return count;
}

OpenFile* Table::get(int fd) {
    return fd >= 0 && fd < MAX_FD ? entries_[fd].file.get() : nullptr;
}

Error Table::close(int fd) {
    ENSURE(get(fd), Error::Invalid);
    entries_[fd] = Entry{};
    return Error::None;
}

void Table::close_all() {
    for (auto& entry : entries_)
        entry = Entry{};
}

Result<int> Table::duplicate_cloexec(int fd, int minimum) {
    OpenFile* source = get(fd);
    ENSURE(source && minimum >= 0 && minimum < MAX_FD, Error::Invalid);
    return install(source->share(), true, minimum);
}

Error Table::duplicate_to(int old_fd, int new_fd) {
    OpenFile* source = get(old_fd);
    ENSURE(source && new_fd >= 0 && new_fd < MAX_FD, Error::Invalid);
    if (old_fd == new_fd)
        return Error::None;
    // Acquire the new reference before dropping the target. Last-close cleanup
    // can sleep, so do not wrap descriptor replacement in an interrupt guard.
    entries_[new_fd].file = source->share();
    entries_[new_fd].close_on_exec = false;
    return Error::None;
}

Error Table::fork_from(const Table& parent, ForkPolicy policy) {
    ENSURE(this != &parent, Error::Invalid);
    ENSURE(policy == ForkPolicy::Reset || policy == ForkPolicy::Share || policy == ForkPolicy::Spawn, Error::Invalid);
    close_all();
    if (policy != ForkPolicy::Reset) {
        for (int i = 0; i < MAX_FD; ++i) {
            const auto& entry = parent.entries_[i];
            if (entry.file && (policy != ForkPolicy::Spawn || !entry.close_on_exec))
                entries_[i] = Entry{entry.file->share(), entry.close_on_exec};
        }
    }
    return Error::None;
}

}  // namespace fd
