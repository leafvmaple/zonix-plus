#pragma once

#include <base/types.h>
#include "lib/result.h"
#include "fs/vfs.h"

namespace fd {

inline constexpr int MAX_FD = 16;

enum class ForkPolicy : uint8_t {
    Reset = 0,
    Share = 1,
};

struct Entry {
    vfs::FileHandle file;
    size_t offset{};
    bool used{};

    inline void set(vfs::FileHandle f, size_t off, bool in_use) {
        file = sys::move(f);
        offset = off;
        used = in_use;
    }
    inline void reset() {
        file.reset();
        offset = 0;
        used = false;
    }
};

class Table {
public:
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;

    void init();
    // Consume the owner; failure closes it, success transfers it to the table.
    Result<int> alloc(vfs::FileHandle file);
    Entry* get(int fd);
    Error close(int fd);
    void close_all();
    Error fork_from(const Table& parent, ForkPolicy policy);

private:
    Entry entries_[MAX_FD]{};
};

}  // namespace fd