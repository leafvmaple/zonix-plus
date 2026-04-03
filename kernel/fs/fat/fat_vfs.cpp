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

    int read(void* buf, size_t size, size_t offset) override {
        if (!is_valid(buf, size, offset))
            return -1;

        return fat_->read_file(&entry_, static_cast<uint8_t*>(buf), static_cast<uint32_t>(offset),
                               static_cast<uint32_t>(size));
    }

    int write(const void* buf, size_t size, size_t offset) override {
        if (!is_valid(buf, size, offset))
            return -1;

        return fat_->write_file(&entry_, static_cast<const uint8_t*>(buf), static_cast<uint32_t>(offset),
                                static_cast<uint32_t>(size));
    }

    int stat(vfs::Stat* st) override {
        if (!st) {
            return -1;
        }

        st->set(entry_.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry_.file_size, entry_.attr);
        return 0;
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
    int mount(BlockDevice* dev) override { return fat_.mount(dev); }
    void unmount() override { fat_.unmount(); }

    int open(const char* relpath, vfs::File** out_file) override {
        if (!relpath || !out_file || relpath[0] == '\0') {
            return -1;
        }

        FatDirEntry entry{};
        if (fat_.find_file(relpath, &entry) != 0) {
            return -1;
        }

        if (entry.is_directory()) {
            return -1;
        }

        auto* file = new (std::nothrow) FatFile(&fat_, entry);
        if (!file) {
            return -1;
        }

        *out_file = file;
        return 0;
    }

    int stat(const char* relpath, vfs::Stat* st) override {
        if (!relpath || !st) {
            return -1;
        }

        if (relpath[0] == '\0') {
            st->set(vfs::NodeType::Directory, 0, FAT_ATTR_DIRECTORY);
            return 0;
        }

        FatDirEntry entry{};
        if (fat_.find_file(relpath, &entry) != 0) {
            return -1;
        }

        st->set(entry.is_directory() ? vfs::NodeType::Directory : vfs::NodeType::File, entry.file_size, entry.attr);
        return 0;
    }

    int readdir(const char* relpath, vfs::DirVisitor& visitor) override {
        if (!relpath) {
            return -1;
        }

        VfsDirVisitor bridge(visitor);
        return fat_.read_dir(relpath, bridge);
    }

    int mkdir(const char* relpath) override {
        if (!relpath || relpath[0] == '\0') {
            return -1;
        }
        return fat_.mkdir(relpath);
    }

    int create(const char* relpath) override {
        if (!relpath || relpath[0] == '\0') {
            return -1;
        }
        return fat_.create_file(relpath);
    }

    int unlink(const char* relpath) override {
        if (!relpath || relpath[0] == '\0') {
            return -1;
        }
        return fat_.unlink(relpath);
    }

    int rmdir(const char* relpath) override {
        if (!relpath || relpath[0] == '\0') {
            return -1;
        }
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
