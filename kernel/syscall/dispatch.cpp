#include "syscall.h"
#include "sched/sched.h"
#include "lib/stdio.h"
#include <abi/syscall.h>
#include <kernel/power.h>
#include "fs/vfs.h"
#include "fs/readiness.h"

namespace syscall {

bool dispatch(Task* task, long nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, long& result) {
    switch (nr) {
        case NR_EXIT:
            if (!task) {
                result = -1;
                return true;
            }
            klog::debug("[PID %d] exited with code %ld\n", task->pid, static_cast<long>(arg0));
            sched::exit(static_cast<int>(arg0));
            return true;
        case NR_SPAWN:
            result = spawn(task, reinterpret_cast<const char*>(arg0), static_cast<long>(arg1), arg2);
            return true;
        case NR_SPAWN_FOREGROUND:
            result = spawn(task, reinterpret_cast<const char*>(arg0), static_cast<long>(arg1), arg2, true);
            return true;
        case NR_CHDIR: result = chdir(task, reinterpret_cast<const char*>(arg0)); return true;
        case NR_GETCWD: result = getcwd(task, arg0, static_cast<size_t>(arg1)); return true;
        case NR_WAIT: result = wait(task, static_cast<long>(arg0), arg1); return true;
        case NR_PROCESS_NEXT: result = process_next(task, static_cast<long>(arg0), arg1); return true;
        case NR_GETPID: result = task ? task->pid : static_cast<long>(Error::Invalid); return true;
        case NR_KILL:
            result = arg0 <= 0x7FFFFFFF ? static_cast<long>(TaskManager::terminate(static_cast<int>(arg0)))
                                        : static_cast<long>(Error::Invalid);
            return true;
        case NR_YIELD:
            sched::schedule();
            result = 0;
            return true;
        case NR_MKDIR: result = mkdir(task, reinterpret_cast<const char*>(arg0)); return true;
        case NR_UNLINK: result = unlink(task, reinterpret_cast<const char*>(arg0)); return true;
        case NR_RMDIR: result = rmdir(task, reinterpret_cast<const char*>(arg0)); return true;
        case NR_FSTAT: result = fstat(task, static_cast<int>(arg0), arg1); return true;
        case NR_FTRUNCATE: result = ftruncate(task, static_cast<int>(arg0), static_cast<long>(arg1)); return true;
        case NR_SYNC: result = static_cast<long>(vfs::sync()); return true;
        case NR_SHUTDOWN: result = static_cast<long>(power::shutdown()); return true;
        case NR_STAT: result = stat(task, reinterpret_cast<const char*>(arg0), arg1); return true;
        case NR_READDIR:
            result = readdir(task, reinterpret_cast<const char*>(arg0), static_cast<long>(arg1), arg2);
            return true;
        case NR_OPEN:
            result = open(task, reinterpret_cast<const char*>(arg0), static_cast<int>(arg1), static_cast<int>(arg2));
            return true;
        case NR_READ:
            result = read(task, static_cast<int>(arg0), reinterpret_cast<void*>(arg1), static_cast<size_t>(arg2));
            return true;
        case NR_IOCTL: result = ioctl(task, static_cast<long>(arg0), static_cast<long>(arg1), arg2); return true;
        case NR_WAIT_READABLE: {
            auto ready = fd::Readiness::wait(task, arg0);
            result = ready.ok() ? static_cast<long>(ready.value()) : static_cast<long>(ready.error());
            return true;
        }
        case NR_OPENPTY: result = openpty(task, arg0, arg1); return true;
        case NR_PIPE2: result = pipe2(task, arg0, static_cast<long>(arg1)); return true;
        case NR_DUP2: result = dup2(task, static_cast<long>(arg0), static_cast<long>(arg1)); return true;
        case NR_FCNTL:
            result = fcntl(task, static_cast<long>(arg0), static_cast<long>(arg1), static_cast<long>(arg2));
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
