#include "syscall.h"
#include "sched/sched.h"
#include <abi/terminal.h>
#include <abi/display.h>

static_assert(sizeof(TerminalSize) == 8);

long syscall::ioctl(Task* task, long fd, long request, uintptr_t argument) {
    if (!task || fd < 0 || fd >= fd::MAX_FD)
        return static_cast<long>(Error::Invalid);
    auto* entry = task->files().get(static_cast<int>(fd));
    if (!entry)
        return static_cast<long>(Error::Invalid);
    if (request >= DISPLAY_GET_INFO && request <= DISPLAY_READ_EVENT)
        return display_control(task, entry->file(), static_cast<unsigned int>(request), argument);
    if (request == TERMINAL_GET_SIZE) {
        if (!vmm::user_range_valid(task->memory(), argument, sizeof(TerminalSize), true))
            return static_cast<long>(Error::Invalid);
        TerminalSize size{};
        Error error = entry->file()->control(TERMINAL_GET_SIZE, &size);
        if (error != Error::None)
            return static_cast<long>(error);
        return static_cast<long>(vmm::copy_to_user(task->memory(), argument, &size, sizeof(size)));
    }
    if (request == TERMINAL_SET_RAW || request == TERMINAL_JOB) {
        if (argument > (request == TERMINAL_JOB ? TERMINAL_JOB_CANCEL : 1))
            return static_cast<long>(Error::Invalid);
        unsigned int mode = static_cast<unsigned int>(argument);
        return static_cast<long>(entry->file()->control(static_cast<unsigned int>(request), &mode));
    }
    return static_cast<long>(Error::NotSupported);
}
