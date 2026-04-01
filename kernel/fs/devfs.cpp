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
    int mount(BlockDevice*) override { return 0; }

    void unmount() override {}

    int open(const char* relpath, vfs::File** out_file) override {
        if (!relpath || !out_file || relpath[0] == '\0') {
            return -1;
        }

        for (const auto& entry : s_char_dev_registry) {
            if (strcmp(entry.name, relpath) == 0) {
                *out_file = entry.create();
                return *out_file ? 0 : -1;
            }
        }

        return -1;
    }

    int stat(const char* relpath, vfs::Stat* st) override {
        if (!relpath || !st) {
            return -1;
        }

        if (relpath[0] == '\0') {
            st->set(vfs::NodeType::Directory, 0, 0);
            return 0;
        }

        for (const auto& entry : s_char_dev_registry) {
            if (strcmp(entry.name, relpath) == 0) {
                st->set(vfs::NodeType::CharDevice, 0, 0);
                return 0;
            }
        }

        return -1;
    }

    int readdir(const char* relpath, vfs::fnReadDir cb, void* arg) override {
        if (!relpath || !cb) {
            return -1;
        }

        if (relpath[0] != '\0') {
            return -1;
        }

        int count = 0;
        for (const auto& entry : s_char_dev_registry) {
            cb({entry.name, vfs::NodeType::CharDevice, 0, 0}, arg);
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

int register_char_dev(const char* name, CharDevFactory factory) {
    if (!name || !factory) {
        return -1;
    }

    if (s_char_dev_registry.full()) {
        cprintf("devfs: registry full, cannot register '%s'\n", name);
        return -1;
    }

    s_char_dev_registry.push_back({name, factory});
    return 0;
}

}  // namespace vfs
