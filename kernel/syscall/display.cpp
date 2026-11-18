#include "syscall.h"
#include "drivers/display.h"
#include "lib/kernel_buffer.h"
#include "sched/sched.h"

static_assert(sizeof(DisplayInfo) == 16 && sizeof(DisplayPresent) == 32 && sizeof(long) == 8 &&
              sizeof(DisplayEvent) == 16);

long syscall::display_control(Task* task, vfs::File* file, unsigned int request, uintptr_t argument) {
    if (request == DISPLAY_GET_INFO) {
        if (!vmm::user_range_valid(task->memory(), argument, sizeof(DisplayInfo), true))
            return static_cast<long>(Error::Invalid);
        DisplayInfo info{};
        Error error = file->control(request, &info);
        return static_cast<long>(error == Error::None ? vmm::copy_to_user(task->memory(), argument, &info, sizeof(info))
                                                      : error);
    }
    if (request == DISPLAY_ACQUIRE) {
        if (argument != 0 && argument != DISPLAY_ACQUIRE_EVENTS)
            return static_cast<long>(Error::Invalid);
        auto flags = static_cast<unsigned int>(argument);
        return static_cast<long>(file->control(request, &flags));
    }
    if (request == DISPLAY_RELEASE)
        return static_cast<long>(argument == 0 ? file->control(request, nullptr) : Error::Invalid);
    if (request == DISPLAY_READ_EVENT) {
        if (!vmm::user_range_valid(task->memory(), argument, sizeof(DisplayEvent), true))
            return static_cast<long>(Error::Invalid);
        DisplayEvent event{};
        Error error = file->control(request, &event);
        if (error != Error::None)
            return static_cast<long>(error);
        return static_cast<long>(vmm::copy_to_user(task->memory(), argument, &event, sizeof(event)));
    }
    DisplayPresent rectangle{};
    Error error = vmm::copy_from_user(task->memory(), &rectangle, argument, sizeof(rectangle));
    if (error != Error::None)
        return static_cast<long>(error);
    error = file->control(display::CHECK_OWNER, nullptr);
    if (error != Error::None)
        return static_cast<long>(error);
    DisplayInfo info{};
    error = file->control(DISPLAY_GET_INFO, &info);
    if (error != Error::None)
        return static_cast<long>(error);
    size_t row_bytes = static_cast<size_t>(rectangle.width) * sizeof(uint32_t);
    size_t span{};
    if (rectangle.reserved != 0 || rectangle.width == 0 || rectangle.height == 0 || rectangle.x >= info.width ||
        rectangle.y >= info.height || rectangle.width > info.width - rectangle.x ||
        rectangle.height > info.height - rectangle.y || rectangle.stride < row_bytes ||
        __builtin_mul_overflow(static_cast<size_t>(rectangle.height - 1), rectangle.stride, &span) ||
        __builtin_add_overflow(span, row_bytes, &span) ||
        !vmm::user_range_valid(task->memory(), rectangle.pixels, span, false))
        return static_cast<long>(Error::Invalid);
    auto allocation = KernelBuffer::alloc(row_bytes * rectangle.height);
    if (!allocation.ok())
        return static_cast<long>(allocation.error());
    auto pixels = allocation.release_value();
    for (unsigned int row = 0; row < rectangle.height; ++row) {
        if (task->termination_requested())
            return static_cast<long>(Error::Interrupted);
        error = vmm::copy_from_user(task->memory(), pixels.data() + row * row_bytes,
                                    rectangle.pixels + static_cast<size_t>(row) * rectangle.stride, row_bytes);
        if (error != Error::None)
            return static_cast<long>(error);
    }
    display::Blit blit{rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                       reinterpret_cast<const uint32_t*>(pixels.data())};
    return static_cast<long>(file->control(DISPLAY_PRESENT, &blit));
}
