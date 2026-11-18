#include "readiness.h"
#include "drivers/intr.h"
#include "fs/fd.h"
#include "sched/sched.h"

namespace fd {

Result<unsigned int> Readiness::wait(Task* task, uintptr_t mask) {
    ENSURE(task && mask != 0 && mask < (1U << MAX_FD), Error::Invalid);
    intr::Guard guard;
    while (true) {
        ENSURE(!task->termination_requested(), Error::Interrupted);
        unsigned int ready = 0;
        for (int index = 0; index < MAX_FD; ++index) {
            auto bit = 1U << index;
            if ((mask & bit) == 0)
                continue;
            auto* entry = task->files().get(index);
            ENSURE(entry, Error::Invalid);
            if (TRY(entry->file()->read_ready()))
                ready |= bit;
        }
        if (ready != 0)
            return ready;
        changed_.sleep();
    }
}

void Readiness::notify() {
    changed_.wakeup_all();
}

}  // namespace fd
