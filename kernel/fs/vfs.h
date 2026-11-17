#pragma once

#include <base/types.h>
#include <abi/fcntl.h>
#include "lib/result.h"
#include "lib/string.h"
#include <sys/memory.hpp>
#include "lib/activity_gate.h"

struct BlockDevice;

namespace vfs {

enum class NodeType : uint8_t {
    Unknown = 0,
    File = 1,
    Directory = 2,
    CharDevice = 3,
};

struct Stat {
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};
    uint64_t file_id{};  // Backend identity, zero when unavailable.

    inline void set(NodeType t, uint32_t s, uint32_t a) {
        type = t;
        size = s;
        attrs = a;
        file_id = 0;
    }
};

struct DirEntry {
    char name[32]{};
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};

    DirEntry() = default;
    DirEntry(const char* n, NodeType t, uint32_t s, uint32_t a) { set(n, t, s, a); }

    void set(const char* n, NodeType t, uint32_t s, uint32_t a);
};

class File;
struct FileCloser {
    void operator()(File* file) const noexcept;
};
using FileHandle = sys::unique_ptr<File, FileCloser>;

class File {
public:
    virtual ~File();

    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    virtual Result<int> read(void* buf, size_t size, size_t offset) = 0;
    virtual Result<int> write(const void* buf, size_t size, size_t offset) = 0;
    // Select EOF and write atomically; success also reports the resulting position.
    virtual Result<int> append(const void* buf, size_t size, size_t* end_offset) {
        static_cast<void>(buf);
        static_cast<void>(size);
        static_cast<void>(end_offset);
        return Error::NotSupported;
    }
    virtual Error stat(Stat* st) = 0;
    virtual Error truncate(size_t size) {
        static_cast<void>(size);
        return Error::NotSupported;
    }
    uint64_t device_id() const { return device_id_; }

    bool can_read() const { return (open_flags_ & O_ACCMODE) != O_WRONLY; }
    bool can_write() const { return (open_flags_ & O_ACCMODE) != O_RDONLY; }
    bool is_append() const { return (open_flags_ & O_APPEND) != 0; }
    Error begin_io(ActivityGuard& guard) const;

private:
    friend Result<FileHandle> open(const char* path, int flags);
    // Factories produce unrestricted handles; open() installs the requested access mode.
    uint64_t device_id_{};
    int open_flags_{O_RDWR};
    bool storage_backed_{};
    size_t* mount_users_{};
};

class DirVisitor {
public:
    virtual ~DirVisitor() = default;
    virtual int visit(const DirEntry& entry) = 0;
};

int init();
Error mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
Error umount(const char* mount_point);
Error sync();
Error freeze();
void thaw();
bool is_frozen();

// Own the returned handle. get()/operator-> borrow; release() hands ownership off.
Result<FileHandle> open(const char* path, int flags = O_RDONLY);
Result<int> read(File* file, void* buf, size_t size, size_t offset);
Result<int> write(File* file, const void* buf, size_t size, size_t offset, size_t* end_offset = nullptr);
Error truncate(File* file, size_t size);
void close(File* file);

inline void FileCloser::operator()(File* file) const noexcept {
    close(file);
}

// Compatibility boundary: the caller owns *out_file on success, null on failure.
inline Error open(const char* path, File** out_file, int flags = O_RDONLY) {
    ENSURE(out_file, Error::Invalid);
    *out_file = nullptr;
    auto file = TRY(open(path, flags));
    *out_file = file.release();
    return Error::None;
}

Error stat(const char* path, Stat* st);
Result<int> readdir(const char* path, DirVisitor& visitor);

Error mkdir(const char* path);
Error create(const char* path);
Error unlink(const char* path);
Error rmdir(const char* path);

bool is_mounted(const char* mount_point);
const char* mounted_device(const char* mount_point);
void print_mount_info(const char* mount_point);

}  // namespace vfs
