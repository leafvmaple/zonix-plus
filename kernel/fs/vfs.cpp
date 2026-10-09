#include "vfs.h"
#include "vfs_fs.h"

#include <sys/inplace_vector.hpp>
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
    size_t users{};
    bool transitioning{};
};

MountSlot* find_slot(const char* mount_point);

class State {
    friend MountSlot* find_slot(const char* mount_point);
    friend Error vfs::mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
    friend Error vfs::register_fs(const char* name, FsFactory factory);

    inline static sys::inplace_vector<FsEntry, MAX_FS_TYPES> fs_registry_{};
    inline static MountSlot mounts_[] = {
        {"/dev", nullptr, nullptr, nullptr, nullptr},
        {"/mnt", nullptr, nullptr, nullptr, nullptr},
        {"/", nullptr, nullptr, nullptr, nullptr},
    };
};

struct ReleaseMount {
    void operator()(MountSlot* slot) const noexcept {
        intr::Guard guard;
        assert(slot->users != 0);
        --slot->users;
    }
};
using MountRef = sys::unique_ptr<MountSlot, ReleaseMount>;

struct FinishTransition {
    void operator()(MountSlot* slot) const noexcept {
        intr::Guard guard;
        slot->transitioning = false;
    }
};
using MountTransition = sys::unique_ptr<MountSlot, FinishTransition>;

struct ResolvedPath {
    MountRef mount;
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

Result<MountRef> acquire_mount(const char* mount_point) {
    intr::Guard guard;
    MountSlot* slot = find_slot(mount_point);
    ENSURE(slot && slot->fs, Error::NotFound);
    ++slot->users;
    return MountRef(slot);
}

Result<ResolvedPath> resolve_path(const char* path) {
    ENSURE(path, Error::Invalid);
    const char* mount_point;
    const char* relpath;

    if (strcmp(path, "/dev") == 0) {
        mount_point = "/dev";
        relpath = "";
    } else if (str_starts_with(path, "/dev/")) {
        mount_point = "/dev";
        relpath = str_skip_char(path + 5, '/');
    } else if (strcmp(path, "/mnt") == 0) {
        mount_point = "/mnt";
        relpath = "";
    } else if (str_starts_with(path, "/mnt/")) {
        mount_point = "/mnt";
        relpath = str_skip_char(path + 5, '/');
    } else if (path[0] == '/') {
        mount_point = "/";
        relpath = str_skip_char(path + 1, '/');
    } else {
        mount_point = "/";
        relpath = str_skip_char(path, '/');
    }

    return ResolvedPath{TRY(acquire_mount(mount_point)), relpath};
}

}  // namespace

File::~File() {
    intr::Guard guard;
    if (mount_users_) {
        assert(*mount_users_ != 0);
        --*mount_users_;
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

    MountSlot* slot;
    FsFactory factory{};
    {
        intr::Guard guard;
        slot = find_slot(mount_point);
        ENSURE(slot, Error::NotFound);
        ENSURE(!slot->transitioning, Error::Busy);
        ENSURE(!slot->fs, Error::Exists);
        for (const auto& entry : State::fs_registry_) {
            if (strcmp(entry.name, fs_type) == 0) {
                factory = entry.create;
                break;
            }
        }
        ENSURE(factory, Error::NotFound);
        slot->transitioning = true;
    }

    // Reserve the slot across allocation and mount(), both of which may sleep.
    MountTransition transition(slot);
    auto owner = sys::unique_ptr<FileSystem>(factory());
    ENSURE(owner, Error::NoMem);
    TRY(owner->mount(dev));

    {
        intr::Guard guard;
        slot->fs_type = fs_type;
        slot->device = dev;
        slot->device_name = dev ? dev->name : nullptr;
        slot->fs = owner.release();
    }

    return Error::None;
}

Error umount(const char* mount_point) {
    ENSURE(mount_point, Error::Invalid);
    MountSlot* slot;
    sys::unique_ptr<FileSystem> owner;
    {
        intr::Guard guard;
        slot = find_slot(mount_point);
        ENSURE(slot, Error::NotFound);
        ENSURE(!slot->transitioning, Error::Busy);
        ENSURE(slot->fs, Error::NotFound);
        ENSURE(slot->users == 0, Error::Busy);
        slot->transitioning = true;
        owner.reset(slot->fs);
        slot->fs = nullptr;
        slot->fs_type = nullptr;
        slot->device = nullptr;
        slot->device_name = nullptr;
    }
    MountTransition transition(slot);
    owner->unmount();
    // Destroy the filesystem before making its slot available to another mount.
    owner.reset();

    return Error::None;
}

Result<FileHandle> open(const char* path) {
    auto resolved = TRY(resolve_path(path));
    ENSURE(resolved.relpath[0] != '\0', Error::Invalid);
    auto file = TRY(resolved.mount->fs->open(resolved.relpath));
    ENSURE(file, Error::Io);
    assert(!file->mount_users_);
    file->mount_users_ = &resolved.mount.release()->users;
    return file;
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
    ENSURE(st, Error::Invalid);

    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->stat(resolved.relpath, st);
}

Result<int> readdir(const char* path, DirVisitor& visitor) {
    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->readdir(resolved.relpath, visitor);
}

Error mkdir(const char* path) {
    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->mkdir(resolved.relpath);
}

Error create(const char* path) {
    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->create(resolved.relpath);
}

Error unlink(const char* path) {
    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->unlink(resolved.relpath);
}

Error rmdir(const char* path) {
    auto resolved = TRY(resolve_path(path));
    return resolved.mount->fs->rmdir(resolved.relpath);
}

bool is_mounted(const char* mount_point) {
    intr::Guard guard;
    MountSlot* slot = find_slot(mount_point);
    return slot != nullptr && slot->fs != nullptr;
}

const char* mounted_device(const char* mount_point) {
    intr::Guard guard;
    MountSlot* slot = find_slot(mount_point);
    if (!slot || !slot->fs) {
        return nullptr;
    }
    return slot->device_name;
}

void print_mount_info(const char* mount_point) {
    auto result = acquire_mount(mount_point);
    if (!result.ok()) {
        cprintf("vfs: %s is not mounted\n", mount_point ? mount_point : "(null)");
        return;
    }
    auto slot = result.release_value();

    cprintf("  FS: %s\n", slot->fs_type ? slot->fs_type : "unknown");
    if (slot->device_name) {
        cprintf("  Device: %s\n", slot->device_name);
    }

    slot->fs->print();
}

Error register_fs(const char* name, FsFactory factory) {
    ENSURE(name && factory, Error::Invalid);
    intr::Guard guard;
    ENSURE(State::fs_registry_.try_push_back(FsEntry{name, factory}), Error::Full);
    return Error::None;
}

}  // namespace vfs
