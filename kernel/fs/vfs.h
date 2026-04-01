#pragma once

#include <base/types.h>
#include "lib/string.h"

struct BlockDevice;

namespace vfs {

enum class NodeType : uint8_t {
    Unknown = 0,
    File = 1,
    Directory = 2,
    CharDevice = 3,
};

struct Stat {
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};

    inline void set(NodeType t, uint32_t s, uint32_t a) {
        type = t;
        size = s;
        attrs = a;
    }
};

struct DirEntry {
    char name[32]{};
    NodeType type{NodeType::Unknown};
    uint32_t size{};
    uint32_t attrs{};

    DirEntry() = default;
    DirEntry(const char* n, NodeType t, uint32_t s, uint32_t a) { set(n, t, s, a); }

    void set(const char* n, NodeType t, uint32_t s, uint32_t a);
};

class File {
public:
    virtual ~File() = default;

    virtual int read(void* buf, size_t size, size_t offset) = 0;
    virtual int write(const void* buf, size_t size, size_t offset) = 0;
    virtual int stat(Stat* st) = 0;
};

using fnReadDir = int (*)(const DirEntry& entry, void* arg);

int init();
int mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
int umount(const char* mount_point);

int open(const char* path, File** out_file);
int read(File* file, void* buf, size_t size, size_t offset);
int write(File* file, const void* buf, size_t size, size_t offset);
void close(File* file);

int stat(const char* path, Stat* st);
int readdir(const char* path, fnReadDir cb, void* arg);

bool is_mounted(const char* mount_point);
const char* mounted_device(const char* mount_point);
void print_mount_info(const char* mount_point);

}  // namespace vfs
