#pragma once

#include "vfs.h"
#include "block/blk.h"

namespace vfs {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual Error mount(BlockDevice* dev) = 0;
    virtual void unmount() = 0;
    virtual Error open(const char* relpath, File** out_file) = 0;
    virtual Error stat(const char* relpath, Stat* st) = 0;
    virtual Result<int> readdir(const char* relpath, DirVisitor& visitor) = 0;
    virtual Error mkdir(const char* relpath) {
        static_cast<void>(relpath);
        return Error::NotSupported;
    }
    virtual Error create(const char* relpath) {
        static_cast<void>(relpath);
        return Error::NotSupported;
    }
    virtual Error unlink(const char* relpath) {
        static_cast<void>(relpath);
        return Error::NotSupported;
    }
    virtual Error rmdir(const char* relpath) {
        static_cast<void>(relpath);
        return Error::NotSupported;
    }
    virtual void print() = 0;
};

using FsFactory = FileSystem* (*)();
Error register_fs(const char* name, FsFactory factory);

using CharDevFactory = File* (*)();
Error register_char_dev(const char* name, CharDevFactory factory);

}  // namespace vfs
