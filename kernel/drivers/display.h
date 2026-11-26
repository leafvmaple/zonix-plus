#pragma once

#include <abi/display.h>
#include <kernel/boot_info.h>
#include "lib/result.h"

namespace display {
// Kernel-only borrowed view. The display subsystem owns the MMIO mapping.
struct Surface {
    uint32_t* pixels{};
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
    FramebufferFormat format{};
};

// Supplies a blit's XRGB8888 pixels one row at a time, so that they are
// copied straight into the surface.
class BlitSource {
public:
    virtual Error copy_row(uint32_t row, uint32_t* out, uint32_t width) = 0;

protected:
    ~BlitSource() = default;
};

struct Blit {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    BlitSource* source;
};

// Internal File::control admission check; not accepted as a user ioctl.
inline constexpr unsigned int CHECK_OWNER = 0x57FF;
int init();
Surface surface();
Error info(DisplayInfo& out);
Error present(const Blit& blit);
// Makes CPU writes to a rectangle of the surface visible to the display
// controller. Every writer calls it; it costs nothing on coherent scanout.
void flush(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// A DISPLAY_CURSOR_SIZE square ARGB image and the pixel of it that marks the
// cursor position.
struct CursorImage {
    const uint32_t* pixels;
    uint32_t hot_x;
    uint32_t hot_y;
};

// The hardware pointer. Each request returns NotSupported on a display
// without a cursor plane; hide_cursor works on any display.
Error show_cursor(const CursorImage& image);
Error move_cursor(uint32_t x, uint32_t y);
void hide_cursor();

// A framebuffer in RAM that a platform display driver scans out, for
// machines whose firmware hands over none.
struct PlatformFramebuffer {
    uintptr_t pa;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    FramebufferFormat format;
    bool coherent;  // false: writes need flush_platform before the controller sees them
};

// Implemented by each architecture. probe_platform returns NoDevice when the
// board has no display controller the kernel drives.
Error probe_platform(PlatformFramebuffer& out);
void flush_platform(uintptr_t pa, size_t byte_count);
// The platform's cursor plane: show_cursor_platform copies the image and
// shows it with its hot pixel at (x, y). A platform without one returns
// NotSupported from both requests and ignores hide_cursor_platform.
Error show_cursor_platform(const CursorImage& image, uint32_t x, uint32_t y);
Error move_cursor_platform(uint32_t x, uint32_t y);
void hide_cursor_platform();
}  // namespace display
