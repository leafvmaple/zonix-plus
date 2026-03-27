#include "fs/fat.h"
#include "fs/vfs.h"

#include "lib/memory.h"

namespace fat {
namespace {

void fill_stat_from_entry(const FatDirEntry& entry, vfs::Stat* st) {
    st->attrs = entry.attr;
    st->size = entry.file_size;
    st->type = (entry.attr & FAT_ATTR_DIRECTORY) ? vfs::NodeType::Directory : vfs::NodeType::File;
}

struct ReadDirBridge {
    vfs::ReadDirFn cb{};
    void* arg{};
};

int fat_readdir_bridge(FatDirEntry* entry, void* arg) {
    auto* bridge = static_cast<ReadDirBridge*>(arg);
    if (!bridge || !bridge->cb) {
        return -1;
    }

    vfs::DirEntry dir{};
    FatInfo::get_filename(entry, dir.name, sizeof(dir.name));
    dir.attrs = entry->attr;
    dir.size = entry->file_size;
    dir.type = (entry->attr & FAT_ATTR_DIRECTORY) ? vfs::NodeType::Directory : vfs::NodeType::File;

    return bridge->cb(&dir, bridge->arg);
}

class FatFile : public vfs::File {
public:
    FatFile(FatInfo* fat, const FatDirEntry& entry) : fat_(fat), entry_(entry) {}

    int read(void* buf, size_t size, size_t offset) override {
        if (!fat_ || !buf) {
            return -1;
        }

        if (size > 0xFFFFFFFFU || offset > 0xFFFFFFFFU) {
            return -1;
        }

        return fat_->read_file(&entry_, static_cast<uint8_t*>(buf), static_cast<uint32_t>(offset),
                               static_cast<uint32_t>(size));
    }

    int write(const void* buf, size_t size, size_t offset) override {
        if (!fat_ || !buf) {
            return -1;
        }

        if (size > 0xFFFFFFFFU || offset > 0xFFFFFFFFU) {
            return -1;
        }

        return fat_->write_file(&entry_, static_cast<const uint8_t*>(buf), static_cast<uint32_t>(offset),
                                static_cast<uint32_t>(size));
    }

    int stat(vfs::Stat* st) override {
        if (!st) {
            return -1;
        }

        fill_stat_from_entry(entry_, st);
        return 0;
    }

private:
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

        if (entry.attr & FAT_ATTR_DIRECTORY) {
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
            st->type = vfs::NodeType::Directory;
            st->size = 0;
            st->attrs = FAT_ATTR_DIRECTORY;
            return 0;
        }

        FatDirEntry entry{};
        if (fat_.find_file(relpath, &entry) != 0) {
            return -1;
        }

        fill_stat_from_entry(entry, st);
        return 0;
    }

    int readdir(const char* relpath, vfs::ReadDirFn cb, void* arg) override {
        if (!relpath || !cb) {
            return -1;
        }

        ReadDirBridge bridge{};
        bridge.cb = cb;
        bridge.arg = arg;

        return fat_.read_dir(relpath, fat_readdir_bridge, &bridge);
    }

    void print() override { fat_.print(); }

private:
    FatInfo fat_{};
};

}  // namespace

vfs::FileSystem* create_vfs_filesystem() {
    return new (std::nothrow) FatFileSystem();
}

}  // namespace fat
