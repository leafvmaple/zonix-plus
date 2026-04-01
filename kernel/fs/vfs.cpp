#include "vfs.h"
#include "vfs_fs.h"

#include "lib/array.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"

namespace vfs {

namespace {

struct FsEntry {
    const char* name{};
    FsFactory create{};
};

constexpr int MAX_FS_TYPES = 8;
Array<FsEntry, MAX_FS_TYPES> s_fs_registry{};

struct MountSlot {
    const char* mount_point{};
    BlockDevice* device{};
    const char* device_name{};
    FileSystem* fs{};
    const char* fs_type{};
};

MountSlot s_mounts[] = {
    {"/dev", nullptr, nullptr, nullptr, nullptr},
    {"/mnt", nullptr, nullptr, nullptr, nullptr},
    {"/", nullptr, nullptr, nullptr, nullptr},
};

struct ResolveResult {
    MountSlot* slot{};
    const char* relpath{};
};

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

    if (strcmp(path, "/dev") == 0) {
        out->slot = find_slot("/dev");
        out->relpath = "";
    } else if (str_starts_with(path, "/dev/")) {
        out->slot = find_slot("/dev");
        out->relpath = str_skip_char(path + 5, '/');
    } else if (strcmp(path, "/mnt") == 0) {
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

void DirEntry::set(const char* n, NodeType t, uint32_t s, uint32_t a) {
    strncpy(name, n, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    type = t;
    size = s;
    attrs = a;
}

int init() {
    return mount("/dev", nullptr, "devfs");
}

// dev may be nullptr for virtual filesystems (e.g. devfs).
int mount(const char* mount_point, BlockDevice* dev, const char* fs_type) {
    if (!mount_point || !fs_type) {
        return -1;
    }

    MountSlot* slot = find_slot(mount_point);
    if (!slot || slot->fs != nullptr) {
        return -1;
    }

    FileSystem* fs = nullptr;
    for (const auto& entry : s_fs_registry) {
        if (strcmp(entry.name, fs_type) == 0) {
            fs = entry.create();
            break;
        }
    }

    if (!fs) {
        return -1;
    }

    if (fs->mount(dev) != 0) {
        delete fs;
        return -1;
    }

    slot->fs = fs;
    slot->fs_type = fs_type;
    slot->device = dev;
    slot->device_name = dev ? dev->name : nullptr;

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
    slot->fs_type = nullptr;
    slot->device = nullptr;
    slot->device_name = nullptr;

    return 0;
}

int open(const char* path, File** out_file) {
    if (!path || !out_file) {
        return -1;
    }

    *out_file = nullptr;

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

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return -1;
    }

    return rr.slot->fs->stat(rr.relpath, st);
}

int readdir(const char* path, fnReadDir cb, void* arg) {
    if (!path || !cb) {
        return -1;
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

    cprintf("  FS: %s\n", slot->fs_type ? slot->fs_type : "unknown");
    if (slot->device_name) {
        cprintf("  Device: %s\n", slot->device_name);
    }

    slot->fs->print();
}

int register_fs(const char* name, FsFactory factory) {
    if (!name || !factory) {
        return -1;
    }

    if (s_fs_registry.full()) {
        cprintf("vfs: register_fs: registry full, cannot register '%s'\n", name);
        return -1;
    }

    s_fs_registry.push_back({name, factory});
    return 0;
}

}  // namespace vfs
