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

struct Blit {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    const uint32_t* pixels;
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
}  // namespace display
