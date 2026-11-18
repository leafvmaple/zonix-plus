#include "display.h"
#include "cons/cons.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"
#include "mm/mmio.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include <kernel/log.h>

extern BootInfo __kernel_boot_info;

namespace display {
namespace {
class Screen {
public:
    static Error init() {
        const auto& boot = __kernel_boot_info;
        if (boot.framebuffer_type != 1 || boot.framebuffer_pa == 0)
            return Error::None;
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

private:
    inline static vmm::MmioRegion mapping_;
    inline static Surface surface_{};
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
        if (request == DISPLAY_READ_EVENT)
            return cons::read_display_event(this, *static_cast<DisplayEvent*>(value));
        if (request == DISPLAY_PRESENT)
            return present(*static_cast<const Blit*>(value));
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

Error info(DisplayInfo& out) {
    auto fb = surface();
    ENSURE(fb.pixels, Error::NoDevice);
    out = DisplayInfo{fb.width, fb.height, fb.width * 4U, DISPLAY_FORMAT_XRGB8888};
    return Error::None;
}

Error present(const Blit& blit) {
    auto fb = surface();
    ENSURE(fb.pixels, Error::NoDevice);
    ENSURE(blit.pixels && blit.width != 0 && blit.height != 0 && blit.x < fb.width && blit.y < fb.height &&
               blit.width <= fb.width - blit.x && blit.height <= fb.height - blit.y,
           Error::Invalid);
    for (uint32_t row = 0; row < blit.height; ++row) {
        ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
        auto* dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb.pixels) +
                                                static_cast<size_t>(blit.y + row) * fb.stride) +
                    blit.x;
        const auto* src = blit.pixels + static_cast<size_t>(row) * blit.width;
        if (fb.format == FramebufferFormat::Xrgb8888)
            memcpy(dst, src, static_cast<size_t>(blit.width) * sizeof(uint32_t));
        else
            for (uint32_t col = 0; col < blit.width; ++col) {
                uint32_t pixel = src[col];
                dst[col] = ((pixel & 0xFF) << 16) | (pixel & 0xFF00) | ((pixel >> 16) & 0xFF);
            }
    }
    return Error::None;
}
}  // namespace display
