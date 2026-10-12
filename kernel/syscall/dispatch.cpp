#include "syscall.h"
#include "sched/sched.h"
#include "lib/stdio.h"
#include <abi/syscall.h>

namespace syscall {

bool dispatch(Task* task, long nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, long& result) {
    switch (nr) {
        case NR_EXIT:
            if (!task) {
                result = -1;
                return true;
            }
            cprintf("[PID %d] exited with code %ld\n", task->pid, static_cast<long>(arg0));
            sched::exit(static_cast<int>(arg0));
            return true;
        case NR_OPEN:
            result = open(task, reinterpret_cast<const char*>(arg0), static_cast<int>(arg1), static_cast<int>(arg2));
            return true;
        case NR_READ:
            result = read(task, static_cast<int>(arg0), reinterpret_cast<void*>(arg1), static_cast<size_t>(arg2));
            return true;
        case NR_CLOSE: result = close(task, static_cast<int>(arg0)); return true;
        case NR_WRITE:
            result =
                write(task, static_cast<int>(arg0), reinterpret_cast<const char*>(arg1), static_cast<size_t>(arg2));
            return true;
        default: return false;
    }
}

}  // namespace syscall
