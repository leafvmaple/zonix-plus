#include "fs/vfs.h"
#include "fs/vfs_fs.h"

#include "lib/array.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "drivers/intr.h"

namespace {

struct CharDevEntry {
    const char* name{};
    vfs::CharDevFactory create{};
};

constexpr int MAX_CHAR_DEVS = 8;
class State {
public:
    static Array<CharDevEntry, MAX_CHAR_DEVS> devices() {
        intr::Guard guard;
        return devices_;
    }

private:
    friend Error vfs::register_char_dev(const char* name, vfs::CharDevFactory factory);
    inline static Array<CharDevEntry, MAX_CHAR_DEVS> devices_{};
};

class DevFileSystem : public vfs::FileSystem {
public:
    Error mount(BlockDevice*) override { return Error::None; }

    void unmount() override {}

    Result<vfs::FileHandle> open(const char* relpath) override {
        ENSURE(relpath && relpath[0] != '\0', Error::Invalid);

        for (const auto& entry : State::devices()) {
            if (strcmp(entry.name, relpath) == 0) {
                auto file = TRY(entry.create());
                ENSURE(file, Error::Io);
                return file;
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

        for (const auto& entry : State::devices()) {
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
        for (const auto& entry : State::devices()) {
            visitor.visit({entry.name, vfs::NodeType::CharDevice, 0, 0});
            count++;
        }
        return count;
    }

    void print() override {
        const auto devices = State::devices();
        cprintf("devfs: %zu device(s) registered\n", devices.size());
        for (const auto& entry : devices) {
            cprintf("  /dev/%s\n", entry.name);
        }
    }
};

vfs::FileSystem* create_dev_filesystem() {
    return new (sys::nothrow) DevFileSystem();
}

struct DevFsRegistrar {
    DevFsRegistrar() { assert(vfs::register_fs("devfs", create_dev_filesystem) == Error::None); }
} devfs_registrar;

}  // namespace

namespace vfs {

Error register_char_dev(const char* name, CharDevFactory factory) {
    ENSURE(name && name[0] != '\0' && factory, Error::Invalid);
    intr::Guard guard;
    for (const auto& entry : State::devices_) {
        ENSURE(strcmp(entry.name, name) != 0, Error::Exists);
    }
    ENSURE(!State::devices_.full(), Error::Full);
    State::devices_.push_back({name, factory});
    return Error::None;
}

}  // namespace vfs
