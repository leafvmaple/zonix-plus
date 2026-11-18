#include "readiness.h"
#include "drivers/intr.h"
#include "fs/fd.h"
#include "sched/sched.h"
#include "trap/trap.h"

namespace fd {

Result<unsigned int> Readiness::wait(Task* task, uintptr_t read_mask, int timeout_ms, uintptr_t write_mask) {
    auto mask = read_mask | write_mask;
    ENSURE(task && mask != 0 && mask < (1U << MAX_FD), Error::Invalid);
    intr::Guard guard;
    bool timed = timeout_ms >= 0;
    uint64_t deadline = timed ? trap::uptime_ms() + static_cast<unsigned int>(timeout_ms) : 0;
    while (true) {
        ENSURE(!task->termination_requested(), Error::Interrupted);
        unsigned int ready = 0;
        for (int index = 0; index < MAX_FD; ++index) {
            auto bit = 1U << index;
            if ((mask & bit) == 0)
                continue;
            auto* entry = task->files().get(index);
            ENSURE(entry, Error::Invalid);
            if ((read_mask & bit) != 0 && TRY(entry->file()->read_ready()))
                ready |= bit;
            if ((write_mask & bit) != 0 && TRY(entry->file()->write_ready()))
                ready |= bit << MAX_FD;
        }
        if (ready != 0)
            return ready;
        if (timed && trap::uptime_ms() >= deadline)
            return 0U;
        (timed ? timed_ : changed_).sleep();
    }
}

void Readiness::notify() {
    changed_.wakeup_all();
    timed_.wakeup_all();
}

void Readiness::tick() {
    if (!timed_.empty())
        timed_.wakeup_all();
}

}  // namespace fd
