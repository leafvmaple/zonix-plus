#include "lib/stdio.h"
#include "lib/stdarg.h"
#include "drivers/intr.h"

#include "assert.h"
#include "cons/cons.h"

namespace {

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

        va_list ap;
        va_start(ap, fmt);
        cprintf("kernel panic at %s:%d:\n    ", file, line);
        vcprintf(fmt, ap);
        cprintf("\n");

        va_end(ap);
    }

    intr::disable();
    while (true) {
        ;
    }
}
