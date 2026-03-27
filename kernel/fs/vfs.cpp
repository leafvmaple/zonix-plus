#include "vfs.h"

#include "fat.h"
#include "cons/cons.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"

namespace vfs {

namespace {

enum class FsKind : uint8_t {
    None = 0,
    Fat = 1,
};

struct MountSlot {
    const char* mount_point{};
    BlockDevice* device{};
    const char* device_name{};
    FileSystem* fs{};
    FsKind kind{FsKind::None};
};

MountSlot s_mounts[] = {
    {"/", nullptr, nullptr, nullptr, FsKind::None},
    {"/mnt", nullptr, nullptr, nullptr, FsKind::None},
};

struct ResolveResult {
    MountSlot* slot{};
    const char* relpath{};
};

class ConsoleFile : public File {
public:
    int read(void* buf, size_t size, size_t offset) override {
        (void)offset;
        if (!buf) {
            return -1;
        }

        auto* out = static_cast<char*>(buf);
        for (size_t i = 0; i < size; i++) {
            out[i] = cons::getc();
        }
        return static_cast<int>(size);
    }

    int write(const void* buf, size_t size, size_t offset) override {
        (void)offset;
        if (!buf) {
            return -1;
        }

        const auto* in = static_cast<const char*>(buf);
        for (size_t i = 0; i < size; i++) {
            cons::putc(in[i]);
        }
        return static_cast<int>(size);
    }

    int stat(Stat* st) override {
        if (!st) {
            return -1;
        }

        st->type = NodeType::CharDevice;
        st->size = 0;
        st->attrs = 0;
        return 0;
    }
};

int open_device(const char* path, File** out_file) {
    if (!path || !out_file) {
        return -1;
    }

    if (strcmp(path, "/dev/console") != 0) {
        return -1;
    }

    auto* file = new (std::nothrow) ConsoleFile();
    if (!file) {
        return -1;
    }

    *out_file = file;
    return 0;
}

int stat_device(const char* path, Stat* st) {
    if (!path || !st) {
        return -1;
    }

    if (strcmp(path, "/dev") == 0) {
        st->type = NodeType::Directory;
        st->size = 0;
        st->attrs = 0;
        return 0;
    }

    if (strcmp(path, "/dev/console") == 0) {
        st->type = NodeType::CharDevice;
        st->size = 0;
        st->attrs = 0;
        return 0;
    }

    return -1;
}

int readdir_device(const char* path, ReadDirFn cb, void* arg) {
    if (!path || !cb) {
        return -1;
    }

    if (strcmp(path, "/dev") != 0) {
        return -1;
    }

    DirEntry entry{};
    strcpy(entry.name, "console");
    entry.type = NodeType::CharDevice;
    entry.size = 0;
    entry.attrs = 0;
    (void)cb(&entry, arg);
    return 1;
}

MountSlot* find_slot(const char* mount_point) {
    if (!mount_point) {
        return nullptr;
    }

    for (auto& slot : s_mounts) {
        if (strcmp(slot.mount_point, mount_point) == 0) {
            return &slot;
        }
    }

    return nullptr;
}

int resolve_path(const char* path, ResolveResult* out) {
    if (!path || !out) {
        return -1;
    }

    if (strcmp(path, "/mnt") == 0) {
        out->slot = find_slot("/mnt");
        out->relpath = "";
    } else if (str_starts_with(path, "/mnt/")) {
        out->slot = find_slot("/mnt");
        out->relpath = str_skip_char(path + 5, '/');
    } else if (path[0] == '/') {
        out->slot = find_slot("/");
        out->relpath = str_skip_char(path + 1, '/');
    } else {
        out->slot = find_slot("/");
        out->relpath = str_skip_char(path, '/');
    }

    if (!out->slot || !out->slot->fs) {
        return -1;
    }

    if (!out->relpath) {
        return -1;
    }

    return 0;
}

}  // namespace

int mount(const char* mount_point, BlockDevice* dev, const char* fs_type) {
    if (!mount_point || !dev || !fs_type) {
        return -1;
    }

    MountSlot* slot = find_slot(mount_point);
    if (!slot || slot->fs != nullptr) {
        return -1;
    }

    FileSystem* fs = nullptr;
    FsKind kind = FsKind::None;

    if (strcmp(fs_type, "fat") == 0) {
        fs = fat::create_vfs_filesystem();
        kind = FsKind::Fat;
    }

    if (!fs) {
        return -1;
    }

    if (fs->mount(dev) != 0) {
        delete fs;
        return -1;
    }

    slot->fs = fs;
    slot->kind = kind;
    slot->device = dev;
    slot->device_name = dev->name;

    return 0;
}

int umount(const char* mount_point) {
    MountSlot* slot = find_slot(mount_point);
    if (!slot || !slot->fs) {
        return -1;
    }

    slot->fs->unmount();
    delete slot->fs;

    slot->fs = nullptr;
    slot->kind = FsKind::None;
    slot->device = nullptr;
    slot->device_name = nullptr;

    return 0;
}

int open(const char* path, File** out_file) {
    if (!path || !out_file) {
        return -1;
    }

    *out_file = nullptr;

    if (open_device(path, out_file) == 0) {
        return 0;
    }

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return -1;
    }

    if (rr.relpath[0] == '\0') {
        return -1;
    }

    return rr.slot->fs->open(rr.relpath, out_file);
}

int read(File* file, void* buf, size_t size, size_t offset) {
    if (!file || !buf) {
        return -1;
    }

    return file->read(buf, size, offset);
}

int write(File* file, const void* buf, size_t size, size_t offset) {
    if (!file || !buf) {
        return -1;
    }

    return file->write(buf, size, offset);
}

void close(File* file) {
    delete file;
}

int stat(const char* path, Stat* st) {
    if (!path || !st) {
        return -1;
    }

    if (stat_device(path, st) == 0) {
        return 0;
    }

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return -1;
    }

    return rr.slot->fs->stat(rr.relpath, st);
}

int readdir(const char* path, ReadDirFn cb, void* arg) {
    if (!path || !cb) {
        return -1;
    }

    int dev_count = readdir_device(path, cb, arg);
    if (dev_count >= 0) {
        return dev_count;
    }

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return -1;
    }

    return rr.slot->fs->readdir(rr.relpath, cb, arg);
}

bool is_mounted(const char* mount_point) {
    MountSlot* slot = find_slot(mount_point);
    return slot != nullptr && slot->fs != nullptr;
}

const char* mounted_device(const char* mount_point) {
    MountSlot* slot = find_slot(mount_point);
    if (!slot || !slot->fs) {
        return nullptr;
    }
    return slot->device_name;
}

void print_mount_info(const char* mount_point) {
    MountSlot* slot = find_slot(mount_point);
    if (!slot || !slot->fs) {
        cprintf("vfs: %s is not mounted\n", mount_point ? mount_point : "(null)");
        return;
    }

    cprintf("  FS: %s\n", slot->kind == FsKind::Fat ? "fat" : "unknown");
    if (slot->device_name) {
        cprintf("  Device: %s\n", slot->device_name);
    }

    slot->fs->print();
}

}  // namespace vfs
