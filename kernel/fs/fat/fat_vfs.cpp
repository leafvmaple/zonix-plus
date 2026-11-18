#include "fs/fat.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"
#include "lib/mutex.h"

#include <sys/intrusive_list.hpp>
#include "lib/memory.h"

namespace fat {
namespace {

class VfsDirVisitor : public FatInfo::DirVisitor {
public:
    explicit VfsDirVisitor(vfs::DirVisitor& outer) : outer_(outer) {}

    int visit(const FatInfo::DirectoryRecord& record) override {
        char short_name[13];
        const char* name = record.long_name;
        if (!name) {
            record.entry.get_filename(short_name, sizeof(short_name));
            name = short_name;
        }
        const auto& entry = record.entry;
        return outer_.visit(
            {name, entry.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry.file_size, entry.attr});
    }

private:
    vfs::DirVisitor& outer_;
};

class FatFileSystem;

class FatFile : public vfs::File {
public:
    explicit FatFile(FatFileSystem& fs) : fs_(fs) {}
    ~FatFile() override;

    Result<int> read(void* buf, size_t size, size_t offset) override;
    Result<int> write(const void* buf, size_t size, size_t offset) override;
    Result<int> append(const void* buf, size_t size, size_t* end_offset) override;
    Error stat(vfs::Stat* st) override;
    Error truncate(size_t size) override;

private:
    friend class FatFileSystem;
    FatFileSystem& fs_;
    FatInfo::EntryLocation location_{};
    sys::list_node<FatFile> link_{*this};
};

class FatFileSystem : public vfs::FileSystem {
public:
    Error mount(BlockDevice* dev) override { return fat_.mount(dev); }
    Error unmount() override { return fat_.unmount(); }
    Error sync() override {
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.sync();
    }

    Result<vfs::FileHandle> open(const char* relpath, int flags) override {
        ENSURE(relpath && relpath[0] != '\0', Error::Invalid);
        const char* name = relpath;
        for (const char* p = relpath; *p; ++p) {
            if (*p == '/') {
                name = p + 1;
            }
        }
        ENSURE(*name && strcmp(name, ".") != 0 && strcmp(name, "..") != 0, Error::Invalid);
        sys::lock_guard<Mutex> guard(mutex_);
        // Allocate the owner before creating or truncating an on-disk file.
        auto file = sys::unique_ptr<FatFile>(new (sys::nothrow) FatFile(*this));
        ENSURE(file, Error::NoMem);
        FatDirEntry entry{};
        Error error = fat_.find_file(relpath, &entry, &file->location_);
        if (error == Error::NotFound && (flags & O_CREAT) != 0) {
            TRY(fat_.create_file(relpath));
            TRY(fat_.find_file(relpath, &entry, &file->location_));
        } else {
            TRY(error);
            ENSURE((flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL), Error::Exists);
        }
        ENSURE(!entry.is_directory(), Error::Invalid);
        ENSURE((flags & O_ACCMODE) == O_RDONLY || (entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
        if ((flags & O_TRUNC) != 0) {
            TRY(fat_.truncate_file(file->location_));
        }
        files_.push_back(*file);
        return vfs::FileHandle(file.release());
    }

    Error stat(const char* relpath, vfs::Stat* st) override {
        ENSURE(relpath && st, Error::Invalid);
        sys::lock_guard<Mutex> guard(mutex_);
        if (relpath[0] == '\0') {
            st->set(vfs::NodeType::Directory, 0, FAT_ATTR_DIRECTORY);
            return Error::None;
        }
        FatDirEntry entry{};
        TRY(fat_.find_file(relpath, &entry));
        set_stat(entry, st);
        return Error::None;
    }

    Result<int> readdir(const char* relpath, vfs::DirVisitor& visitor) override {
        ENSURE(relpath);
        sys::lock_guard<Mutex> guard(mutex_);
        VfsDirVisitor bridge(visitor);
        return fat_.read_dir(relpath, bridge);
    }

    Error mkdir(const char* relpath) override {
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.mkdir(relpath);
    }

    Error create(const char* relpath) override {
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.create_file(relpath);
    }

    Error rename(const char* relpath, const char* new_name) override {
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.rename(relpath, new_name);
    }

    Error unlink(const char* relpath) override {
        sys::lock_guard<Mutex> guard(mutex_);
        FatDirEntry entry{};
        FatInfo::EntryLocation location{};
        TRY(fat_.find_file(relpath, &entry, &location));
        for (const auto& file : files_) {
            ENSURE(file.location_ != location, Error::Busy);
        }
        return fat_.unlink(relpath);
    }

    Error rmdir(const char* relpath) override {
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.rmdir(relpath);
    }

    void print() override { fat_.print(); }

private:
    friend class FatFile;

    static void set_stat(const FatDirEntry& entry, vfs::Stat* st) {
        st->set(entry.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry.file_size, entry.attr);
    }

    Result<int> read(const FatInfo::EntryLocation& location, void* buf, size_t size, size_t offset) {
        ENSURE((buf || size == 0) && size <= 0x7FFFFFFFU && offset <= 0xFFFFFFFFU, Error::Invalid);
        sys::lock_guard<Mutex> guard(mutex_);
        FatDirEntry entry{};
        TRY(fat_.read_file_entry(location, &entry));
        return fat_.read_file(&entry, static_cast<uint8_t*>(buf), static_cast<uint32_t>(offset),
                              static_cast<uint32_t>(size));
    }

    Result<int> write(const FatInfo::EntryLocation& location, const void* buf, size_t size, size_t offset, bool append,
                      size_t* end_offset) {
        ENSURE((buf || size == 0) && size <= 0x7FFFFFFFU && offset <= 0xFFFFFFFFU, Error::Invalid);
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.write_file(location, static_cast<const uint8_t*>(buf), static_cast<uint32_t>(offset),
                               static_cast<uint32_t>(size), append, end_offset);
    }

    Error truncate(const FatInfo::EntryLocation& location, size_t size) {
        ENSURE(size == 0, Error::NotSupported);
        sys::lock_guard<Mutex> guard(mutex_);
        return fat_.truncate_file(location);
    }

    Error stat(const FatInfo::EntryLocation& location, vfs::Stat* st) {
        ENSURE(st, Error::Invalid);
        sys::lock_guard<Mutex> guard(mutex_);
        FatDirEntry entry{};
        TRY(fat_.read_file_entry(location, &entry));
        set_stat(entry, st);
        st->file_id = static_cast<uint64_t>(location.sector) * (BlockDevice::BLOCK_SIZE_BYTES / sizeof(FatDirEntry)) +
                      location.index + 1;
        return Error::None;
    }

    FatInfo fat_{};
    Mutex mutex_{};
    // Borrows live FatFile objects; each VFS handle owns its file and pins this mount.
    sys::intrusive_list<FatFile, &FatFile::link_> files_{};
};

FatFile::~FatFile() {
    if (link_.is_linked()) {
        sys::lock_guard<Mutex> guard(fs_.mutex_);
        fs_.files_.erase(fs_.files_.iterator_to(*this));
    }
}

Result<int> FatFile::read(void* buf, size_t size, size_t offset) {
    return fs_.read(location_, buf, size, offset);
}

Result<int> FatFile::write(const void* buf, size_t size, size_t offset) {
    return fs_.write(location_, buf, size, offset, false, nullptr);
}

Result<int> FatFile::append(const void* buf, size_t size, size_t* end_offset) {
    return fs_.write(location_, buf, size, 0, true, end_offset);
}

Error FatFile::truncate(size_t size) {
    return fs_.truncate(location_, size);
}

Error FatFile::stat(vfs::Stat* st) {
    ActivityGuard activity;
    TRY(begin_io(activity));
    return fs_.stat(location_, st);
}

}  // namespace

vfs::FileSystem* create_vfs_filesystem() {
    return new (sys::nothrow) FatFileSystem();
}

namespace {

struct FatFsRegistrar {
    FatFsRegistrar() { assert(vfs::register_fs("fat", fat::create_vfs_filesystem) == Error::None); }
} fat_registrar;

}  // namespace

}  // namespace fat
