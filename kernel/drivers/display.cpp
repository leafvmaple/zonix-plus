#include "display.h"
#include "drivers/intr.h"
#include "cons/cons.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"
#include "mm/mmio.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include <kernel/address.h>
#include <kernel/log.h>

extern BootInfo __kernel_boot_info;

namespace display {
namespace {
class Screen {
public:
    static Error init() {
        const auto& boot = __kernel_boot_info;
        if (boot.framebuffer_type != 1 || boot.framebuffer_pa == 0)
            return init_platform();
        ENSURE(boot.framebuffer_bpp == 32 && boot.framebuffer_width != 0 && boot.framebuffer_height != 0 &&
                   (boot.framebuffer_format == FramebufferFormat::Xrgb8888 ||
                    boot.framebuffer_format == FramebufferFormat::Xbgr8888),
               Error::NotSupported);
        size_t row_bytes = static_cast<size_t>(boot.framebuffer_width) * sizeof(uint32_t);
        ENSURE(row_bytes <= boot.framebuffer_pitch && row_bytes <= 0xFFFFFFFF, Error::Invalid);
        size_t byte_count{};
        ENSURE(
            !__builtin_mul_overflow(static_cast<size_t>(boot.framebuffer_pitch), boot.framebuffer_height, &byte_count),
            Error::Invalid);
        auto mapping = TRY(vmm::map_mmio(boot.framebuffer_pa, byte_count, VM_WRITE | VM_NOCACHE));
        surface_ = Surface{reinterpret_cast<uint32_t*>(mapping->address()), boot.framebuffer_width,
                           boot.framebuffer_height, boot.framebuffer_pitch, boot.framebuffer_format};
        mapping_ = sys::move(mapping);
        return Error::None;
    }
    static Surface surface() { return surface_; }

    static void flush(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        if (coherent_ || !surface_.pixels || x >= surface_.width || y >= surface_.height)
            return;
        width = width < surface_.width - x ? width : surface_.width - x;
        height = height < surface_.height - y ? height : surface_.height - y;
        const uintptr_t first = pa_ + static_cast<uintptr_t>(y) * surface_.stride + x * sizeof(uint32_t);
        if (width == surface_.width) {
            flush_platform(first, static_cast<size_t>(height) * surface_.stride);
            return;
        }
        for (uint32_t row = 0; row < height; ++row)
            flush_platform(first + static_cast<uintptr_t>(row) * surface_.stride, width * sizeof(uint32_t));
    }

private:
    // RAM the kernel already maps directly; the platform driver owns it.
    static Error init_platform() {
        PlatformFramebuffer fb{};
        Error error = probe_platform(fb);
        if (error == Error::NoDevice)
            return Error::None;
        TRY(error);
        ENSURE(fb.width != 0 && fb.height != 0 && fb.stride >= fb.width * sizeof(uint32_t), Error::Invalid);
        surface_ = Surface{phys_to_virt<uint32_t>(fb.pa), fb.width, fb.height, fb.stride, fb.format};
        pa_ = fb.pa;
        coherent_ = fb.coherent;
        klog::info("display: %ux%u from the platform display controller\n", fb.width, fb.height);
        return Error::None;
    }

    inline static vmm::MmioRegion mapping_;
    inline static Surface surface_{};
    inline static uintptr_t pa_{};
    inline static bool coherent_{true};
};

// Where the hardware pointer is and whether the platform shows it.
class Cursor {
public:
    static Error show(const CursorImage& image) {
        intr::Guard guard;
        ENSURE(Screen::surface().pixels, Error::NoDevice);
        ENSURE(image.pixels && image.hot_x < DISPLAY_CURSOR_SIZE && image.hot_y < DISPLAY_CURSOR_SIZE, Error::Invalid);
        TRY(show_cursor_platform(image, x_, y_));
        visible_ = true;
        return Error::None;
    }
    static Error move(uint32_t x, uint32_t y) {
        intr::Guard guard;
        auto fb = Screen::surface();
        ENSURE(fb.pixels, Error::NoDevice);
        ENSURE(x < fb.width && y < fb.height, Error::Invalid);
        // A hidden cursor only keeps the position for its next image.
        if (visible_)
            TRY(move_cursor_platform(x, y));
        x_ = x;
        y_ = y;
        return Error::None;
    }
    static void hide() {
        intr::Guard guard;
        if (!visible_)
            return;
        hide_cursor_platform();
        visible_ = false;
    }

private:
    inline static uint32_t x_{};
    inline static uint32_t y_{};
    inline static bool visible_{};
};

class DisplayFile : public vfs::File {
public:
    ~DisplayFile() override {
        if (owner_pid_ != 0)
            cons::release_display(this);
    }
    Result<bool> read_ready() override { return cons::display_event_ready(this); }
    Result<int> read(void*, size_t, size_t) override { return Error::NotSupported; }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return Error::None;
    }
    Error control(unsigned int request, void* value) override {
        if (request == DISPLAY_GET_INFO)
            return info(*static_cast<DisplayInfo*>(value));
        Task* task = sched::current();
        ENSURE(task && can_write(), Error::Access);
        if (request == DISPLAY_ACQUIRE) {
            ENSURE(!cons::owns_display(this), Error::Busy);
            TRY(cons::acquire_display(this, *static_cast<const unsigned int*>(value)));
            owner_pid_ = task->pid;
            return Error::None;
        }
        ENSURE(owner_pid_ != 0 && owner_pid_ == task->pid && cons::owns_display(this), Error::Access);
        if (request == CHECK_OWNER)
            return Error::None;
        if (request == DISPLAY_RELEASE) {
            cons::release_display(this);
            owner_pid_ = 0;
            return Error::None;
        }
        if (request == DISPLAY_READ_TIMED_EVENT)
            return cons::read_display_event(this, *static_cast<DisplayTimedEvent*>(value));
        if (request == DISPLAY_READ_EVENT) {
            DisplayTimedEvent timed{};
            TRY(cons::read_display_event(this, timed));
            *static_cast<DisplayEvent*>(value) = timed.event;
            return Error::None;
        }
        if (request == DISPLAY_PRESENT)
            return present(*static_cast<const Blit*>(value));
        if (request == DISPLAY_SET_CURSOR) {
            if (!value) {
                Cursor::hide();
                return Error::None;
            }
            return Cursor::show(*static_cast<const CursorImage*>(value));
        }
        if (request == DISPLAY_MOVE_CURSOR) {
            const auto& position = *static_cast<const DisplayCursorPosition*>(value);
            return Cursor::move(position.x, position.y);
        }
        return Error::NotSupported;
    }

private:
    int owner_pid_{};
};

Result<vfs::FileHandle> open_display() {
    vfs::FileHandle file(new (sys::nothrow) DisplayFile());
    ENSURE(file, Error::NoMem);
    return file;
}
}  // namespace

int init() {
    Error registered = vfs::register_char_dev("display", open_display);
    if (registered != Error::None)
        return static_cast<int>(registered);
    return static_cast<int>(Screen::init());
}

Surface surface() {
    return Screen::surface();
}

void flush(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    Screen::flush(x, y, width, height);
}

Error show_cursor(const CursorImage& image) {
    return Cursor::show(image);
}

Error move_cursor(uint32_t x, uint32_t y) {
    return Cursor::move(x, y);
}

void hide_cursor() {
    Cursor::hide();
}

Error info(DisplayInfo& out) {
    auto fb = surface();
    ENSURE(fb.pixels, Error::NoDevice);
    out = DisplayInfo{fb.width, fb.height, fb.width * 4U, DISPLAY_FORMAT_XRGB8888};
    return Error::None;
}

Error present(const Blit& blit) {
    auto fb = surface();
    ENSURE(fb.pixels, Error::NoDevice);
    ENSURE(blit.source && blit.width != 0 && blit.height != 0 && blit.x < fb.width && blit.y < fb.height &&
               blit.width <= fb.width - blit.x && blit.height <= fb.height - blit.y,
           Error::Invalid);
    Error error = Error::None;
    uint32_t row = 0;
    for (; row < blit.height && error == Error::None; ++row) {
        if (sched::current()->termination_requested()) {
            error = Error::Interrupted;
            break;
        }
        auto* dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb.pixels) +
                                                static_cast<size_t>(blit.y + row) * fb.stride) +
                    blit.x;
        error = blit.source->copy_row(row, dst, blit.width);
        if (error == Error::None && fb.format != FramebufferFormat::Xrgb8888) {
            for (uint32_t col = 0; col < blit.width; ++col) {
                uint32_t pixel = dst[col];
                dst[col] = ((pixel & 0xFF) << 16) | (pixel & 0xFF00) | ((pixel >> 16) & 0xFF);
            }
        }
    }
    // Rows already written reach the screen even when a later one failed.
    if (row != 0)
        flush(blit.x, blit.y, blit.width, row);
    return error;
}
}  // namespace display
