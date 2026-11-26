#include "syscall.h"
#include "drivers/display.h"
#include "lib/kernel_buffer.h"
#include "sched/sched.h"

static_assert(sizeof(DisplayInfo) == 16 && sizeof(DisplayPresent) == 32 && sizeof(long) == 8 &&
              sizeof(DisplayEvent) == 16 && sizeof(DisplayTimedEvent) == 24 && sizeof(DisplayCursor) == 16 &&
              sizeof(DisplayCursorPosition) == 8);

namespace {

template<typename Record>
long read_event(Task* task, vfs::File* file, unsigned int request, uintptr_t argument) {
    if (!vmm::user_range_valid(task->memory(), argument, sizeof(Record), true))
        return static_cast<long>(Error::Invalid);
    Record event{};
    Error error = file->control(request, &event);
    if (error != Error::None)
        return static_cast<long>(error);
    return static_cast<long>(vmm::copy_to_user(task->memory(), argument, &event, sizeof(event)));
}

// The rows of a validated user rectangle, copied straight into the surface.
class UserRows final : public display::BlitSource {
public:
    UserRows(MemoryDesc* memory, uintptr_t pixels, size_t stride) : memory_(memory), pixels_(pixels), stride_(stride) {}

    Error copy_row(uint32_t row, uint32_t* out, uint32_t width) override {
        return vmm::copy_from_user(memory_, out, pixels_ + static_cast<size_t>(row) * stride_,
                                   static_cast<size_t>(width) * sizeof(uint32_t));
    }

private:
    MemoryDesc* memory_;
    uintptr_t pixels_;
    size_t stride_;
};

// Copies a cursor image in and hands it to the display, or hides the cursor
// for a null image.
long set_cursor(Task* task, vfs::File* file, uintptr_t argument) {
    DisplayCursor cursor{};
    Error error = vmm::copy_from_user(task->memory(), &cursor, argument, sizeof(cursor));
    if (error != Error::None)
        return static_cast<long>(error);
    if (cursor.pixels == 0)
        return static_cast<long>(file->control(DISPLAY_SET_CURSOR, nullptr));
    constexpr size_t image_bytes = DISPLAY_CURSOR_SIZE * DISPLAY_CURSOR_SIZE * sizeof(uint32_t);
    if (!vmm::user_range_valid(task->memory(), static_cast<uintptr_t>(cursor.pixels), image_bytes, false))
        return static_cast<long>(Error::Invalid);
    auto allocation = KernelBuffer::alloc(image_bytes);
    if (!allocation.ok())
        return static_cast<long>(allocation.error());
    auto pixels = allocation.release_value();
    error = vmm::copy_from_user(task->memory(), pixels.data(), static_cast<uintptr_t>(cursor.pixels), image_bytes);
    if (error != Error::None)
        return static_cast<long>(error);
    display::CursorImage image{reinterpret_cast<const uint32_t*>(pixels.data()), cursor.hot_x, cursor.hot_y};
    return static_cast<long>(file->control(DISPLAY_SET_CURSOR, &image));
}

}  // namespace

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
        if (argument != 0 && argument != DISPLAY_ACQUIRE_EVENTS &&
            argument != (DISPLAY_ACQUIRE_EVENTS | DISPLAY_ACQUIRE_KEYBOARD_CTRL_C))
            return static_cast<long>(Error::Invalid);
        auto flags = static_cast<unsigned int>(argument);
        return static_cast<long>(file->control(request, &flags));
    }
    if (request == DISPLAY_RELEASE)
        return static_cast<long>(argument == 0 ? file->control(request, nullptr) : Error::Invalid);
    if (request == DISPLAY_READ_EVENT)
        return read_event<DisplayEvent>(task, file, request, argument);
    if (request == DISPLAY_READ_TIMED_EVENT)
        return read_event<DisplayTimedEvent>(task, file, request, argument);
    if (request == DISPLAY_SET_CURSOR)
        return set_cursor(task, file, argument);
    if (request == DISPLAY_MOVE_CURSOR) {
        DisplayCursorPosition position{};
        Error error = vmm::copy_from_user(task->memory(), &position, argument, sizeof(position));
        return static_cast<long>(error == Error::None ? file->control(request, &position) : error);
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
    UserRows rows(task->memory(), rectangle.pixels, rectangle.stride);
    display::Blit blit{rectangle.x, rectangle.y, rectangle.width, rectangle.height, &rows};
    return static_cast<long>(file->control(DISPLAY_PRESENT, &blit));
}
