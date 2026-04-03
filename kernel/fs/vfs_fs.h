#pragma once

#include "vfs.h"
#include "block/blk.h"

namespace vfs {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual int mount(BlockDevice* dev) = 0;
    virtual void unmount() = 0;
    virtual int open(const char* relpath, File** out_file) = 0;
    virtual int stat(const char* relpath, Stat* st) = 0;
    virtual int readdir(const char* relpath, DirVisitor& visitor) = 0;
    virtual int mkdir(const char* relpath) {
        static_cast<void>(relpath);
        return -1;
    }
    virtual int create(const char* relpath) {
        static_cast<void>(relpath);
        return -1;
    }
    virtual int unlink(const char* relpath) {
        static_cast<void>(relpath);
        return -1;
    }
    virtual int rmdir(const char* relpath) {
        static_cast<void>(relpath);
        return -1;
    }
    virtual void print() = 0;
};

using FsFactory = FileSystem* (*)();
int register_fs(const char* name, FsFactory factory);

using CharDevFactory = File* (*)();
int register_char_dev(const char* name, CharDevFactory factory);

}  // namespace vfs
