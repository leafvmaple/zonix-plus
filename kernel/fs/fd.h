#pragma once

#include <base/types.h>
#include "lib/result.h"
#include "lib/mutex.h"
#include "fs/vfs.h"

namespace fd {

inline constexpr int MAX_FD = 16;
enum class ForkPolicy : uint8_t { Reset = 0, Share = 1, Spawn = 2 };

class OpenFile;
struct ReleaseOpenFile {
    void operator()(OpenFile* file) const noexcept;
};
// Each move-only descriptor owns one reference to a shared open-file description.
using OpenFileRef = sys::unique_ptr<OpenFile, ReleaseOpenFile>;

class OpenFile {
public:
    vfs::File* file() const { return file_.get(); }
    size_t offset() const { return offset_; }
    void set_offset(size_t offset) { offset_ = offset; }
    Error lock_interruptible() { return mutex_.lock_interruptible(); }
    void unlock() { mutex_.unlock(); }

private:
    friend class Table;
    friend struct ReleaseOpenFile;
    OpenFile() = default;
    ~OpenFile() = default;
    OpenFileRef share();
    vfs::FileHandle file_;
    size_t offset_{};
    size_t references_{1};
    Mutex mutex_;
};

class Table {
public:
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;

    void init();
    // Consume the file on success and failure; reserve a description before mutating open().
    Result<int> alloc(vfs::FileHandle file, bool close_on_exec = false);
    Error create_pipe(int (&ends)[2], bool close_on_exec);
    Result<int> open(const char* path, int flags);
    bool has_space() const;
    size_t size() const;
    OpenFile* get(int fd);
    Error close(int fd);
    Result<int> duplicate_cloexec(int fd, int minimum);
    Error duplicate_to(int old_fd, int new_fd);
    void close_all();
    Error fork_from(const Table& parent, ForkPolicy policy);

private:
    struct Entry {
        OpenFileRef file;
        bool close_on_exec{};
    };
    Result<int> install(OpenFileRef file, bool close_on_exec = false, int minimum = 0);
    Entry entries_[MAX_FD]{};
};

}  // namespace fd
