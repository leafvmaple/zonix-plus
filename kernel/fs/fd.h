#pragma once

#include <base/types.h>

namespace vfs {
class File;
}

namespace fd {

inline constexpr int MAX_FD = 16;

enum class ForkPolicy : uint8_t {
    Reset = 0,
    Share = 1,
};

struct Entry {
    vfs::File* file{};
    size_t offset{};
    bool used{};

    inline void set(vfs::File* f, size_t off, bool in_use) {
        file = f;
        offset = off;
        used = in_use;
    }
    inline void reset() { set(nullptr, 0, false); }
};

class Table {
public:
    void init();
    int alloc(vfs::File* file);
    Entry* get(int fd);
    int close(int fd);
    void close_all();
    int fork_from(const Table& parent, ForkPolicy policy);

private:
    Entry entries_[MAX_FD]{};
};

}  // namespace fd