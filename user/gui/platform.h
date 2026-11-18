#pragma once

#include <platform/software/ZCSoftwareRenderDevice.h>

namespace zocos {
class Director;
}

namespace zonix_gui {

inline constexpr const char* UI_FONT_PATH = "/opt/fonts/UI.TTF";

// The caller owns the RGBA surface and keeps it alive until Director::shutdown.
bool init_offscreen(zocos::Director& director, const zocos::PixelBuffer& buffer, zocos::Color4B background);

inline unsigned int xrgb(const unsigned char* rgba) {
    return (static_cast<unsigned int>(rgba[0]) << 16) | (static_cast<unsigned int>(rgba[1]) << 8) | rgba[2];
}

void finish();

}  // namespace zonix_gui
