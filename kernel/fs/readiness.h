#pragma once

#include "lib/result.h"
#include "lib/waitqueue.h"

struct Task;

namespace fd {

class Readiness {
public:
    // Returns the read-ready descriptors as a bit set, with the write-ready
    // set MAX_FD bits above it.
    static Result<uint64_t> wait(Task* task, uintptr_t read_mask, int timeout_ms = -1, uintptr_t write_mask = 0);
    static void notify();
    static void tick();

private:
    inline static WaitQueue changed_{};
    inline static WaitQueue timed_{};
};

}  // namespace fd
