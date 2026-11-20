#include "syscall.h"
#include "sched/sched.h"
#include <abi/terminal.h>
#include <abi/display.h>
#include <abi/net.h>
#include <abi/channel.h>

static_assert(sizeof(TerminalSize) == 8);

long syscall::openpty(Task* task, uintptr_t output, uintptr_t input) {
    if (!task || !vmm::user_range_valid(task->memory(), output, 2 * sizeof(int), true))
        return static_cast<long>(Error::Invalid);
    TerminalSize size{};
    Error error = vmm::copy_from_user(task->memory(), &size, input, sizeof(size));
    if (error != Error::None)
        return static_cast<long>(error);
    int ends[2];
    error = pty::open(task, ends, size);
    if (error != Error::None)
        return static_cast<long>(error);
    error = vmm::copy_to_user(task->memory(), output, ends, sizeof(ends));
    if (error != Error::None) {
        static_cast<void>(task->files().close(ends[0]));
        static_cast<void>(task->files().close(ends[1]));
    }
    return static_cast<long>(error);
}

long syscall::ioctl(Task* task, long fd, long request, uintptr_t argument) {
    if (!task || fd < 0 || fd >= fd::MAX_FD)
        return static_cast<long>(Error::Invalid);
    auto* entry = task->files().get(static_cast<int>(fd));
    if (!entry)
        return static_cast<long>(Error::Invalid);
    if (request >= DISPLAY_GET_INFO && request <= DISPLAY_READ_TIMED_EVENT)
        return display_control(task, entry->file(), static_cast<unsigned int>(request), argument);
    if (request == NET_GET_MAC) {
        uint8_t mac[NET_MAC_SIZE]{};
        if (!vmm::user_range_valid(task->memory(), argument, sizeof(mac), true))
            return static_cast<long>(Error::Invalid);
        Error error = entry->file()->control(NET_GET_MAC, mac);
        if (error != Error::None)
            return static_cast<long>(error);
        return static_cast<long>(vmm::copy_to_user(task->memory(), argument, mac, sizeof(mac)));
    }
    if (request == TERMINAL_GET_SIZE) {
        if (!vmm::user_range_valid(task->memory(), argument, sizeof(TerminalSize), true))
            return static_cast<long>(Error::Invalid);
        TerminalSize size{};
        Error error = entry->file()->control(TERMINAL_GET_SIZE, &size);
        if (error != Error::None)
            return static_cast<long>(error);
        return static_cast<long>(vmm::copy_to_user(task->memory(), argument, &size, sizeof(size)));
    }
    if (request == TERMINAL_SET_SIZE) {
        TerminalSize size{};
        Error error = vmm::copy_from_user(task->memory(), &size, argument, sizeof(size));
        if (error != Error::None)
            return static_cast<long>(error);
        return static_cast<long>(entry->file()->control(TERMINAL_SET_SIZE, &size));
    }
    if (request == CHANNEL_SHUTDOWN_WRITE) {
        if (argument != 0)
            return static_cast<long>(Error::Invalid);
        return static_cast<long>(entry->file()->control(CHANNEL_SHUTDOWN_WRITE, nullptr));
    }
    if (request == TERMINAL_SET_RAW || request == TERMINAL_JOB) {
        if (argument > (request == TERMINAL_JOB ? TERMINAL_JOB_CANCEL : 1))
            return static_cast<long>(Error::Invalid);
        unsigned int mode = static_cast<unsigned int>(argument);
        return static_cast<long>(entry->file()->control(static_cast<unsigned int>(request), &mode));
    }
    return static_cast<long>(Error::NotSupported);
}
