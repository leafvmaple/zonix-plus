#pragma once

#include <base/types.h>
#include "lib/result.h"
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

    virtual Result<int> read(void* buf, size_t size, size_t offset) = 0;
    virtual Result<int> write(const void* buf, size_t size, size_t offset) = 0;
    virtual Error stat(Stat* st) = 0;
};

class DirVisitor {
public:
    virtual ~DirVisitor() = default;
    virtual int visit(const DirEntry& entry) = 0;
};

int init();
Error mount(const char* mount_point, BlockDevice* dev, const char* fs_type);
Error umount(const char* mount_point);

Error open(const char* path, File** out_file);
Result<int> read(File* file, void* buf, size_t size, size_t offset);
Result<int> write(File* file, const void* buf, size_t size, size_t offset);
void close(File* file);

Error stat(const char* path, Stat* st);
Result<int> readdir(const char* path, DirVisitor& visitor);

Error mkdir(const char* path);
Error create(const char* path);
Error unlink(const char* path);
Error rmdir(const char* path);

bool is_mounted(const char* mount_point);
const char* mounted_device(const char* mount_point);
void print_mount_info(const char* mount_point);

}  // namespace vfs
