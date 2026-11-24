#include "lib/stdio.h"
#include "lib/stdarg.h"
#include "drivers/intr.h"

#include "assert.h"
#include "backtrace.h"
#include "cons/cons.h"
#include <kernel/log.h>

namespace {

constexpr size_t PANIC_MESSAGE_SIZE = 200;

class Panic {
public:
    // True the first time only; a panic raised while reporting one is not reported.
    static bool enter() {
        bool first = !active_;
        active_ = true;
        return first;
    }

private:
    inline static bool active_{};
};

}  // namespace

void panic_at(const char* file, int line, const char* fmt, ...) {
    if (Panic::enter()) {
        cons::emergency_restore();

        // The report goes into the kernel log as well as the console, so it is
        // kept with the records that led up to it.
        char message[PANIC_MESSAGE_SIZE];
        va_list ap;
        va_start(ap, fmt);
        sys::vsnprintf(message, sizeof(message), fmt, ap);
        va_end(ap);
        klog::error("kernel panic at %s:%d:\n", file, line);
        klog::error("    %s\n", message);
        debug::log_backtrace("panic called from", reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
                             reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
    }

    intr::disable();
    while (true) {
        ;
    }
}
