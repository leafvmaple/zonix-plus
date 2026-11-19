#include "fs/vfs.h"
#include "lib/stdio.h"
#include "sched/sched.h"

// Runs a kernel thread off the bottom of its stack. The kernel must stop with
// a stack-overflow panic, so this fixture never returns to the suite; a runner
// selects it with a marker file and reads the serial console.

namespace {

// Each call takes more stack. The bound is volatile so the compiler keeps the
// recursion, and the frame is written so every page of it is touched.
[[gnu::noinline]] int descend(int depth, volatile int* limit) {
    volatile char frame[256];
    for (unsigned int i = 0; i < sizeof(frame); i += 64) {
        frame[i] = static_cast<char>(depth);
    }
    if (depth >= *limit) {
        return frame[0];
    }
    return descend(depth + 1, limit) + frame[0];
}

int overflow(void*) {
    volatile int limit = 0x7FFFFFFF;
    return descend(0, &limit);
}

}  // namespace

namespace stack_overflow_e2e {

// Only TEST=1 kernels inspect this marker, installed on temporary images by the runner.
bool run() {
    auto marker = vfs::open("/KSTACK.RUN", O_RDONLY);
    if (!marker.ok()) {
        return false;
    }
    marker.release_value().reset();
    cprintf("KSTACK_OVERFLOW_BEGIN\n");
    auto child = sched::kernel_thread(overflow, nullptr);
    if (child.ok()) {
        int code{};
        static_cast<void>(sched::wait(child.value(), &code));
    }
    // Reaching this line means the overflow went unnoticed.
    cprintf("[FAIL] KSTACK_OVERFLOW_NOT_DETECTED\n");
    return true;
}

}  // namespace stack_overflow_e2e
