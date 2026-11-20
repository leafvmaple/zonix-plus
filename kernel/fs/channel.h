#pragma once

#include "fs/vfs.h"

// Named local stream channels. A listener owns a name until its file closes;
// connecting to the name queues one end of a new duplex stream for accept and
// returns the other end at once.
namespace channel {

class Listener;

// The name must be 1..CHANNEL_NAME_MAX bytes; Exists when it is already taken.
Result<vfs::FileHandle> listen(const char* name, bool nonblocking);
// NotFound without a listener, Full when its backlog holds CHANNEL_BACKLOG streams.
Result<vfs::FileHandle> connect(const char* name, bool nonblocking);
// Waits for a queued stream unless the listener is nonblocking (Busy).
Result<vfs::FileHandle> accept(Listener& listener, bool nonblocking);

}  // namespace channel
