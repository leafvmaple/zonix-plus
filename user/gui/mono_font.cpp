#include "mono_font.h"
#include "../command.h"
#include <kernel/psf.h>
#include <sys/vector.hpp>

extern "C" const unsigned char terminal_font[];

namespace zonix_gui {
namespace {

constexpr int FIRST = ' ';
constexpr int LAST = '~';
constexpr int COLS = 16;
constexpr int ROWS = 6;
// The slot after the last character is opaque.
constexpr int SOLID = LAST - FIRST + 1;
static_assert(SOLID == COLS * ROWS - 1);

// Texture rows run downwards from the top of the atlas.
zocos::Rect slot_area(int slot) {
    return {static_cast<float>(slot % COLS) / COLS, 1.f - static_cast<float>(slot / COLS + 1) / ROWS, 1.f / COLS,
            1.f / ROWS};
}

}  // namespace

long MonoFont::init(zocos::RenderDevice& device) {
    psf::Font font{};
    if (!psf::parse(terminal_font, &font) || font.width != WIDTH || font.height != HEIGHT)
        return SYS_ERROR_NOT_SUPPORTED;

    constexpr int atlas_width = COLS * WIDTH;
    constexpr int atlas_height = ROWS * HEIGHT;
    sys::vector<unsigned char> atlas(atlas_width * atlas_height);
    if (atlas.size() != static_cast<sys::size_t>(atlas_width * atlas_height))
        return SYS_ERROR_FULL;
    for (int slot = 0; slot <= SOLID; ++slot) {
        const auto* bits = psf::glyph(&font, FIRST + slot);
        int left = slot % COLS * WIDTH;
        int top = slot / COLS * HEIGHT;
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x)
                atlas[(top + y) * atlas_width + left + x] =
                    slot == SOLID || (bits[y] & (0x80U >> x)) != 0 ? 255 : 0;
    }
    texture_ = device.createTexture({atlas_width, atlas_height, zocos::TextureFormat::A8Unorm, {atlas.data()}});
    return texture_.isValid() ? 0 : SYS_ERROR_IO;
}

zocos::Rect MonoFont::cell(int character) {
    return slot_area(character >= FIRST && character <= LAST ? character - FIRST : '?' - FIRST);
}

zocos::Rect MonoFont::solid() {
    return slot_area(SOLID);
}

}  // namespace zonix_gui
