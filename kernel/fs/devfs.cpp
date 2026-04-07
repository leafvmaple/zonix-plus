#include "fs/vfs.h"
#include "fs/vfs_fs.h"

#include "lib/array.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"

namespace {

struct CharDevEntry {
    const char* name{};
    vfs::CharDevFactory create{};
};

constexpr int MAX_CHAR_DEVS = 8;
Array<CharDevEntry, MAX_CHAR_DEVS> s_char_dev_registry{};

class DevFileSystem : public vfs::FileSystem {
public:
    Error mount(BlockDevice*) override { return Error::None; }

    void unmount() override {}

    Error open(const char* relpath, vfs::File** out_file) override {
        ENSURE(relpath && out_file && relpath[0] != '\0', Error::Invalid);

        for (const auto& entry : s_char_dev_registry) {
            if (strcmp(entry.name, relpath) == 0) {
                *out_file = entry.create();
                return *out_file ? Error::None : Error::NoMem;
            }
        }

        return Error::NotFound;
    }

    Error stat(const char* relpath, vfs::Stat* st) override {
        ENSURE(relpath && st, Error::Invalid);

        if (relpath[0] == '\0') {
            st->set(vfs::NodeType::Directory, 0, 0);
            return Error::None;
        }

        for (const auto& entry : s_char_dev_registry) {
            if (strcmp(entry.name, relpath) == 0) {
                st->set(vfs::NodeType::CharDevice, 0, 0);
                return Error::None;
            }
        }

        return Error::NotFound;
    }

    Result<int> readdir(const char* relpath, vfs::DirVisitor& visitor) override {
        ENSURE(relpath, Error::Invalid);

        if (relpath[0] != '\0') {
            return Error::NotFound;
        }

        int count = 0;
        for (const auto& entry : s_char_dev_registry) {
            visitor.visit({entry.name, vfs::NodeType::CharDevice, 0, 0});
            count++;
        }
        return count;
    }

    void print() override {
        cprintf("devfs: %zu device(s) registered\n", s_char_dev_registry.size());
        for (const auto& entry : s_char_dev_registry) {
            cprintf("  /dev/%s\n", entry.name);
        }
    }
};

vfs::FileSystem* create_dev_filesystem() {
    return new (std::nothrow) DevFileSystem();
}

struct DevFsRegistrar {
    DevFsRegistrar() { vfs::register_fs("devfs", create_dev_filesystem); }
} s_devfs_registrar;

}  // namespace

namespace vfs {

Error register_char_dev(const char* name, CharDevFactory factory) {
    ENSURE(name && factory, Error::Invalid);

    if (s_char_dev_registry.full()) {
        cprintf("devfs: registry full, cannot register '%s'\n", name);
        return Error::Full;
    }

    s_char_dev_registry.push_back({name, factory});
    return Error::None;
}

}  // namespace vfs
