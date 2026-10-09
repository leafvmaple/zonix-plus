#include "vfs.h"
#include "vfs_fs.h"

#include "lib/array.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "drivers/intr.h"

namespace vfs {

namespace {

struct FsEntry {
    const char* name{};
    FsFactory create{};
};

constexpr int MAX_FS_TYPES = 8;

struct MountSlot {
    const char* mount_point{};
    BlockDevice* device{};
    const char* device_name{};
    FileSystem* fs{};
    const char* fs_type{};
    size_t open_files{};
};

MountSlot* find_slot(const char* mount_point);

class State {
    friend MountSlot* find_slot(const char* mount_point);
    friend Error vfs::mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
    friend Error vfs::register_fs(const char* name, FsFactory factory);

    inline static Array<FsEntry, MAX_FS_TYPES> fs_registry_{};
    inline static MountSlot mounts_[] = {
        {"/dev", nullptr, nullptr, nullptr, nullptr},
        {"/mnt", nullptr, nullptr, nullptr, nullptr},
        {"/", nullptr, nullptr, nullptr, nullptr},
    };
};

struct ResolveResult {
    MountSlot* slot{};
    const char* relpath{};
};

MountSlot* find_slot(const char* mount_point) {
    if (!mount_point) {
        return nullptr;
    }

    for (auto& slot : State::mounts_) {
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

File::~File() {
    intr::Guard guard;
    if (mount_open_files_) {
        --*mount_open_files_;
    }
}

void DirEntry::set(const char* n, NodeType t, uint32_t s, uint32_t a) {
    strncpy(name, n, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    type = t;
    size = s;
    attrs = a;
}

int init() {
    return static_cast<int>(mount("/dev", nullptr, "devfs"));
}

// dev may be nullptr for virtual filesystems (e.g. devfs).
Error mount(const char* mount_point, BlockDevice* dev, const char* fs_type) {
    ENSURE(mount_point && fs_type, Error::Invalid);

    MountSlot* slot = find_slot(mount_point);
    if (!slot || slot->fs != nullptr) {
        return Error::NotFound;
    }

    FileSystem* fs = nullptr;
    for (const auto& entry : State::fs_registry_) {
        if (strcmp(entry.name, fs_type) == 0) {
            fs = entry.create();
            break;
        }
    }

    auto owner = sys::unique_ptr<FileSystem>(fs);
    ENSURE(owner, Error::NotFound);
    TRY(owner->mount(dev));

    slot->fs = owner.release();
    slot->fs_type = fs_type;
    slot->device = dev;
    slot->device_name = dev ? dev->name : nullptr;

    return Error::None;
}

Error umount(const char* mount_point) {
    intr::Guard guard;
    MountSlot* slot = find_slot(mount_point);
    if (!slot || !slot->fs) {
        return Error::NotFound;
    }

    if (slot->open_files != 0) {
        return Error::Busy;
    }

    slot->fs->unmount();
    delete slot->fs;

    slot->fs = nullptr;
    slot->fs_type = nullptr;
    slot->device = nullptr;
    slot->device_name = nullptr;

    return Error::None;
}

Error open(const char* path, File** out_file) {
    ENSURE(path && out_file, Error::Invalid);

    *out_file = nullptr;

    ResolveResult rr{};
    // Pin the mount before open(), which may sleep on disk I/O.
    {
        intr::Guard guard;
        if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
            return Error::NotFound;
        }
        if (rr.relpath[0] == '\0') {
            return Error::Invalid;
        }
        ++rr.slot->open_files;
    }
    Error rc = rr.slot->fs->open(rr.relpath, out_file);
    {
        intr::Guard guard;
        if (rc == Error::None && *out_file) {
            (*out_file)->mount_open_files_ = &rr.slot->open_files;
        } else {
            --rr.slot->open_files;
            if (rc == Error::None) {
                rc = Error::Fail;
            }
        }
    }
    return rc;
}

Result<int> read(File* file, void* buf, size_t size, size_t offset) {
    ENSURE(file && buf, Error::Invalid);

    return file->read(buf, size, offset);
}

Result<int> write(File* file, const void* buf, size_t size, size_t offset) {
    ENSURE(file && buf, Error::Invalid);

    return file->write(buf, size, offset);
}

void close(File* file) {
    delete file;
}

Error stat(const char* path, Stat* st) {
    ENSURE(path && st, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->stat(rr.relpath, st);
}

Result<int> readdir(const char* path, DirVisitor& visitor) {
    ENSURE(path, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->readdir(rr.relpath, visitor);
}

Error mkdir(const char* path) {
    ENSURE(path, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->mkdir(rr.relpath);
}

Error create(const char* path) {
    ENSURE(path, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->create(rr.relpath);
}

Error unlink(const char* path) {
    ENSURE(path, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->unlink(rr.relpath);
}

Error rmdir(const char* path) {
    ENSURE(path, Error::Invalid);

    ResolveResult rr{};
    if (resolve_path(path, &rr) != 0 || !rr.slot || !rr.slot->fs) {
        return Error::NotFound;
    }

    return rr.slot->fs->rmdir(rr.relpath);
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

Error register_fs(const char* name, FsFactory factory) {
    ENSURE(name && factory, Error::Invalid);
    ENSURE_LOG(!State::fs_registry_.full(), Error::Full, "vfs: register_fs: registry full, cannot register '%s'", name);

    State::fs_registry_.push_back({name, factory});
    return Error::None;
}

}  // namespace vfs
