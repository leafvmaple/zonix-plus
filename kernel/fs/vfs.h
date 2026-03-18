#pragma once

#include <base/types.h>
#include "block/blk.h"

namespace vfs {

enum class NodeType : uint8_t {
    Unknown = 0,
    File = 1,
    Directory = 2,
};

struct Stat {
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};
};

struct DirEntry {
    char name[32]{};
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};
};

class File {
public:
    virtual ~File() = default;

    virtual int read(void* buf, size_t size, size_t offset) = 0;
    virtual int stat(Stat* st) = 0;
};

using ReadDirFn = int (*)(const DirEntry* entry, void* arg);

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual int mount(BlockDevice* dev) = 0;
    virtual void unmount() = 0;
    virtual int open(const char* relpath, File** out_file) = 0;
    virtual int stat(const char* relpath, Stat* st) = 0;
    virtual int readdir(const char* relpath, ReadDirFn cb, void* arg) = 0;
    virtual void print_info() = 0;
};

int mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
int umount(const char* mount_point);

int open(const char* path, File** out_file);
int read(File* file, void* buf, size_t size, size_t offset);
void close(File* file);

int stat(const char* path, Stat* st);
int readdir(const char* path, ReadDirFn cb, void* arg);

bool is_mounted(const char* mount_point);
const char* mounted_device(const char* mount_point);
void print_mount_info(const char* mount_point);

}  // namespace vfs
