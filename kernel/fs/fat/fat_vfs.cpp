#include "fs/fat.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"

#include "lib/memory.h"

namespace fat {
namespace {

class VfsDirVisitor : public FatInfo::DirVisitor {
public:
    explicit VfsDirVisitor(vfs::DirVisitor& outer) : outer_(outer) {}

    int visit(FatDirEntry* entry) override {
        char name[32];
        entry->get_filename(name, sizeof(name));

        return outer_.visit({name, entry->is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File,
                             entry->file_size, entry->attr});
    }

private:
    vfs::DirVisitor& outer_;
};

class FatFile : public vfs::File {
public:
    FatFile(FatInfo* fat, const FatDirEntry& entry) : fat_(fat), entry_(entry) {}

    Result<int> read(void* buf, size_t size, size_t offset) override {
        ENSURE(is_valid(buf, size, offset));

        return fat_->read_file(&entry_, static_cast<uint8_t*>(buf), static_cast<uint32_t>(offset),
                               static_cast<uint32_t>(size));
    }

    Result<int> write(const void* buf, size_t size, size_t offset) override {
        ENSURE(is_valid(buf, size, offset));

        return fat_->write_file(&entry_, static_cast<const uint8_t*>(buf), static_cast<uint32_t>(offset),
                                static_cast<uint32_t>(size));
    }

    Error stat(vfs::Stat* st) override {
        ENSURE(st);

        st->set(entry_.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry_.file_size, entry_.attr);
        return Error::None;
    }

private:
    bool is_valid(const void* buf, size_t size, size_t offset) const {
        if (!fat_ || !buf)
            return false;

        if (size > 0xFFFFFFFFU || offset > 0xFFFFFFFFU)
            return false;

        return true;
    }

    FatInfo* fat_{};
    FatDirEntry entry_{};
};

class FatFileSystem : public vfs::FileSystem {
public:
    Error mount(BlockDevice* dev) override { return fat_.mount(dev); }
    void unmount() override { fat_.unmount(); }

    Error open(const char* relpath, vfs::File** out_file) override {
        ENSURE(relpath && out_file && relpath[0] != '\0');

        FatDirEntry entry{};
        TRY(fat_.find_file(relpath, &entry));
        ENSURE(!entry.is_directory());  // Use open for files only.

        auto* file = new (std::nothrow) FatFile(&fat_, entry);
        if (!file) {
            return Error::NoMem;
        }

        *out_file = file;
        return Error::None;
    }

    Error stat(const char* relpath, vfs::Stat* st) override {
        ENSURE(relpath && st, Error::Invalid);

        if (relpath[0] == '\0') {
            st->set(vfs::NodeType::Directory, 0, FAT_ATTR_DIRECTORY);
            return Error::None;
        }

        FatDirEntry entry{};
        TRY(fat_.find_file(relpath, &entry));

        st->set(entry.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry.file_size, entry.attr);
        return Error::None;
    }

    Result<int> readdir(const char* relpath, vfs::DirVisitor& visitor) override {
        ENSURE(relpath);

        VfsDirVisitor bridge(visitor);
        return fat_.read_dir(relpath, bridge);
    }

    Error mkdir(const char* relpath) override {
        ENSURE(relpath && relpath[0] != '\0');
        return fat_.mkdir(relpath);
    }

    Error create(const char* relpath) override {
        ENSURE(relpath && relpath[0] != '\0');
        return fat_.create_file(relpath);
    }

    Error unlink(const char* relpath) override {
        ENSURE(relpath && relpath[0] != '\0');
        return fat_.unlink(relpath);
    }

    Error rmdir(const char* relpath) override {
        ENSURE(relpath && relpath[0] != '\0');
        return fat_.rmdir(relpath);
    }

    void print() override { fat_.print(); }

private:
    FatInfo fat_{};
};

}  // namespace

vfs::FileSystem* create_vfs_filesystem() {
    return new (std::nothrow) FatFileSystem();
}

namespace {

struct FatFsRegistrar {
    FatFsRegistrar() { vfs::register_fs("fat", fat::create_vfs_filesystem); }
} s_fat_registrar;

}  // namespace

}  // namespace fat
