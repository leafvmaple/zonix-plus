#pragma once

#include "fs/vfs.h"

namespace pipe {
struct Ends {
    vfs::FileHandle read;
    vfs::FileHandle write;
};
Result<Ends> create(bool nonblocking = false);

// Two connected bidirectional stream ends, one buffer per direction. Each end
// reports the pipe type and accepts CHANNEL_SHUTDOWN_WRITE, after which its
// peer reads end of file once the buffered bytes are gone.
struct DuplexEnds {
    vfs::FileHandle first;
    vfs::FileHandle second;
};
Result<DuplexEnds> create_duplex(bool nonblocking = false);
}  // namespace pipe
