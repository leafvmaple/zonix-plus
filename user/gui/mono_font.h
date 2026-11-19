#pragma once

#include "../desktop/text_frame.h"
#include <base/ZCRenderDevice.h>

namespace zonix_gui {

// The console's fixed-width ASCII font as a texture, one texel per pixel.
class MonoFont {
public:
    static constexpr int WIDTH = desktop_ipc::GLYPH_WIDTH;
    static constexpr int HEIGHT = desktop_ipc::GLYPH_HEIGHT;

    long init(zocos::RenderDevice& device);
    zocos::TextureHandle texture() const { return texture_; }

    // The texture area of a character's cell, from its bottom-left corner.
    // Bytes outside printable ASCII show as '?'.
    static zocos::Rect cell(int character);
    // A fully opaque cell, for cursors and rules.
    static zocos::Rect solid();

private:
    zocos::TextureHandle texture_;
};

}  // namespace zonix_gui
