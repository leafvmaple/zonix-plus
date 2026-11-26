#include "syscall.h"
#include "drivers/entropy.h"
#include "mm/vmm.h"
#include "sched/sched.h"

long syscall::getrandom(Task* task, uintptr_t output, long size, uintptr_t flags) {
    if (!task || size < 0 || flags != 0)
        return static_cast<long>(Error::Invalid);
    if (size == 0)
        return 0;
    size_t count = static_cast<size_t>(size);
    if (count > entropy::MAX_READ)
        count = entropy::MAX_READ;
    if (!vmm::user_range_valid(task->memory(), output, count, true))
        return static_cast<long>(Error::Invalid);

    uint8_t bytes[entropy::MAX_READ];
    auto received = entropy::read(bytes, count);
    if (!received.ok())
        return static_cast<long>(received.error());
    count = received.value();
    Error copied = vmm::copy_to_user(task->memory(), output, bytes, count);
    memset(bytes, 0, count);
    return copied == Error::None ? static_cast<long>(count) : static_cast<long>(copied);
}
