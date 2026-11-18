#pragma once

#include "fs/vfs.h"

namespace pipe {
struct Ends {
    vfs::FileHandle read;
    vfs::FileHandle write;
};
Result<Ends> create(bool nonblocking = false);
}  // namespace pipe
