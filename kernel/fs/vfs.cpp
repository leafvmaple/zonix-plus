#include "vfs.h"

#include "fat.h"
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
    if (!out_file) {
        return -1;
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

void close(File* file) {
    delete file;
}

int stat(const char* path, Stat* st) {
    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return -1;
    }

    return rr.slot->fs->stat(rr.relpath, st);
}

int readdir(const char* path, ReadDirFn cb, void* arg) {
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

    slot->fs->print_info();
}

}  // namespace vfs
