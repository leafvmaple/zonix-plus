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
    virtual int readdir(const char* relpath, fnReadDir cb, void* arg) = 0;
    virtual void print() = 0;
};

}  // namespace vfs
