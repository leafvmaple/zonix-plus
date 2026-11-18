#include "vfs.h"
#include "vfs_fs.h"

#include <debug/assert.h>
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
public:
    static Error begin_io(ActivityGuard& guard) { return guard.acquire(io_gate_); }
    static Error freeze() { return io_gate_.close(); }
    static void thaw() { io_gate_.open(); }
    static bool is_frozen() { return io_gate_.is_closed(); }
    static bool owns_freeze() { return io_gate_.is_owner(); }
    static bool is_mount_name(const char* name) {
        for (const auto& slot : mounts_) {
            const char* part = name;
            const char* mount_name = slot.mount_point + 1;
            while (*part && *mount_name) {
                char ch = *part >= 'A' && *part <= 'Z' ? *part + ('a' - 'A') : *part;
                if (ch != *mount_name)
                    break;
                ++part;
                ++mount_name;
            }
            if (!*part && !*mount_name)
                return true;
        }
        return false;
    }

private:
    friend MountSlot* find_slot(const char* mount_point);
    friend Error vfs::mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
    friend Error vfs::register_fs(const char* name, FsFactory factory);
    friend Error vfs::sync();

    inline static ActivityGate io_gate_{};
    inline static sys::inplace_vector<FsEntry, MAX_FS_TYPES> fs_registry_{};
    inline static MountSlot mounts_[] = {
        {"/dev", nullptr, nullptr, nullptr, nullptr},
        {"/mnt", nullptr, nullptr, nullptr, nullptr},
        {"/opt", nullptr, nullptr, nullptr, nullptr},
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

Error begin_storage_io(const ResolvedPath& path, ActivityGuard& guard) {
    return path.mount->device ? State::begin_io(guard) : Error::None;
}

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
    } else if (strcmp(path, "/opt") == 0 && is_mounted("/opt")) {
        mount_point = "/opt";
        relpath = "";
    } else if (str_starts_with(path, "/opt/") && is_mounted("/opt")) {
        mount_point = "/opt";
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

Error File::begin_io(ActivityGuard& guard) const {
    return storage_backed_ ? State::begin_io(guard) : Error::None;
}

Error freeze() {
    return State::freeze();
}
void thaw() {
    State::thaw();
}
bool is_frozen() {
    return State::is_frozen();
}

Error sync() {
    ActivityGuard activity;
    if (!State::owns_freeze()) {
        TRY(State::begin_io(activity));
    }
    constexpr size_t count = sizeof(State::mounts_) / sizeof(MountSlot);
    sys::inplace_vector<MountRef, count> mounts;
    Error first = Error::None;
    {
        intr::Guard guard;
        for (auto& slot : State::mounts_) {
            if (slot.transitioning) {
                first = Error::Busy;
            } else if (slot.fs) {
                ++slot.users;
                assert(mounts.try_push_back(MountRef(&slot)));
            }
        }
    }
    for (auto& mount : mounts) {
        Error error = mount->fs->sync();
        if (first == Error::None) {
            first = error;
        }
    }
    return first;
}

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
    ActivityGuard activity;
    TRY(State::begin_io(activity));

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
    ActivityGuard activity;
    TRY(State::begin_io(activity));
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
    }
    MountTransition transition(slot);
    Error error = owner->unmount();
    if (error != Error::None) {
        // The backend retains dirty state and detached storage for a later retry.
        intr::Guard guard;
        slot->fs = owner.release();
        return error;
    }
    {
        intr::Guard guard;
        slot->fs_type = nullptr;
        slot->device = nullptr;
        slot->device_name = nullptr;
    }
    // Destroy the filesystem before making its slot available to another mount.
    owner.reset();

    return Error::None;
}

Result<FileHandle> open(const char* path, int flags) {
    constexpr int supported = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND;
    ENSURE((flags & ~supported) == 0 && (flags & O_ACCMODE) != O_ACCMODE, Error::Invalid);
    ENSURE((flags & O_EXCL) == 0 || (flags & O_CREAT) != 0, Error::Invalid);
    ENSURE((flags & (O_TRUNC | O_APPEND)) == 0 || (flags & O_ACCMODE) != O_RDONLY, Error::Invalid);
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    ENSURE(resolved.relpath[0] != '\0', Error::Invalid);
    auto file = TRY(resolved.mount->fs->open(resolved.relpath, flags));
    ENSURE(file, Error::Io);
    assert(!file->mount_users_);
    file->open_flags_ = flags;
    file->storage_backed_ = resolved.mount->device != nullptr;
    for (int i = 0; i < BlockManager::device_count(); ++i) {
        if (BlockManager::find_device(i) == resolved.mount->device) {
            file->device_id_ = static_cast<uint64_t>(i) + 1;
            break;
        }
    }
    file->mount_users_ = &resolved.mount.release()->users;
    return file;
}

Result<int> read(File* file, void* buf, size_t size, size_t offset) {
    ENSURE(file && (buf || size == 0) && size <= 0x7FFFFFFFU, Error::Invalid);
    ENSURE(file->can_read(), Error::Access);
    if (size == 0) {
        return 0;
    }
    ActivityGuard activity;
    TRY(file->begin_io(activity));

    return file->read(buf, size, offset);
}

Result<int> write(File* file, const void* buf, size_t size, size_t offset, size_t* end_offset) {
    ENSURE(file && (buf || size == 0) && size <= 0x7FFFFFFFU, Error::Invalid);
    ENSURE(file->can_write(), Error::Access);
    size_t next_offset = offset;
    int bytes{};
    ActivityGuard activity;
    if (size != 0) {
        TRY(file->begin_io(activity));
        if (file->is_append()) {
            bytes = TRY(file->append(buf, size, &next_offset));
        } else {
            ENSURE(size <= static_cast<size_t>(-1) - offset, Error::Invalid);
            bytes = TRY(file->write(buf, size, offset));
            ENSURE(bytes >= 0 && static_cast<size_t>(bytes) <= size, Error::Io);
            next_offset += static_cast<size_t>(bytes);
        }
        ENSURE(bytes >= 0 && static_cast<size_t>(bytes) <= size, Error::Io);
    }
    if (end_offset) {
        *end_offset = next_offset;
    }

    return bytes;
}

Error truncate(File* file, size_t size) {
    ENSURE(file, Error::Invalid);
    ENSURE(file->can_write(), Error::Access);
    ActivityGuard activity;
    TRY(file->begin_io(activity));
    return file->truncate(size);
}

void close(File* file) {
    delete file;
}

Error stat(const char* path, Stat* st) {
    ENSURE(st, Error::Invalid);

    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    // A published mount supplies its root directory, even when the backend
    // only implements metadata for entries below it.
    if (resolved.relpath[0] == '\0') {
        st->set(NodeType::Directory, 0, 0);
        return Error::None;
    }
    return resolved.mount->fs->stat(resolved.relpath, st);
}

Result<int> readdir(const char* path, DirVisitor& visitor) {
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    return resolved.mount->fs->readdir(resolved.relpath, visitor);
}

Error mkdir(const char* path) {
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    return resolved.mount->fs->mkdir(resolved.relpath);
}

Error create(const char* path) {
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    return resolved.mount->fs->create(resolved.relpath);
}

Error rename(const char* path, const char* new_name) {
    ENSURE(new_name && *new_name && !strchr(new_name, '/') && strcmp(new_name, ".") != 0 && strcmp(new_name, "..") != 0,
           Error::Invalid);
    auto resolved = TRY(resolve_path(path));
    ENSURE(*resolved.relpath, Error::Access);
    if (strcmp(resolved.mount->mount_point, "/") == 0 && !strchr(resolved.relpath, '/')) {
        ENSURE(!State::is_mount_name(resolved.relpath) && !State::is_mount_name(new_name), Error::Access);
    }
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    return resolved.mount->fs->rename(resolved.relpath, new_name);
}

Error unlink(const char* path) {
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
    return resolved.mount->fs->unlink(resolved.relpath);
}

Error rmdir(const char* path) {
    if (path) {
        ENSURE(!State::is_mount_name(path[0] == '/' ? path + 1 : path), Error::Access);
    }
    auto resolved = TRY(resolve_path(path));
    ActivityGuard activity;
    TRY(begin_storage_io(resolved, activity));
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
