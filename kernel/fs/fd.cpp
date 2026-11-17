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

Result<int> Table::install(OpenFileRef file) {
    ENSURE(file && file->file_, Error::Invalid);
    for (int i = 0; i < MAX_FD; ++i) {
        if (!entries_[i]) {
            entries_[i] = sys::move(file);
            return i;
        }
    }
    return Error::Full;
}

Result<int> Table::alloc(vfs::FileHandle file) {
    ENSURE(file, Error::Invalid);
    ENSURE(has_space(), Error::Full);
    OpenFileRef description(new (sys::nothrow) OpenFile());
    ENSURE(description, Error::NoMem);
    description->file_ = sys::move(file);
    return install(sys::move(description));
}

Result<int> Table::open(const char* path, int flags) {
    ENSURE(has_space(), Error::Full);
    OpenFileRef description(new (sys::nothrow) OpenFile());
    ENSURE(description, Error::NoMem);
    description->file_ = TRY(vfs::open(path, flags));
    return install(sys::move(description));
}

bool Table::has_space() const {
    return size() < MAX_FD;
}
size_t Table::size() const {
    size_t count{};
    for (const auto& file : entries_)
        count += static_cast<bool>(file);
    return count;
}

OpenFile* Table::get(int fd) {
    return fd >= 0 && fd < MAX_FD ? entries_[fd].get() : nullptr;
}

Error Table::close(int fd) {
    ENSURE(get(fd), Error::Invalid);
    entries_[fd].reset();
    return Error::None;
}

void Table::close_all() {
    for (auto& file : entries_)
        file.reset();
}

Error Table::fork_from(const Table& parent, ForkPolicy policy) {
    ENSURE(this != &parent, Error::Invalid);
    ENSURE(policy == ForkPolicy::Reset || policy == ForkPolicy::Share, Error::Invalid);
    close_all();
    if (policy == ForkPolicy::Share) {
        for (int i = 0; i < MAX_FD; ++i) {
            if (parent.entries_[i])
                entries_[i] = parent.entries_[i]->share();
        }
    }
    return Error::None;
}

}  // namespace fd
