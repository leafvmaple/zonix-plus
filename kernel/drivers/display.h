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
}  // namespace display
