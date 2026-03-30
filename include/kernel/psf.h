#pragma once

#include <base/types.h>

namespace psf {

struct Psf1Header {
    uint8_t  magic[2];    // 0x36, 0x04
    uint8_t  mode;        // 0=256, 1=512, 2=256+unicode, 3=512+unicode
    uint8_t  charsize;    // bytes per glyph (= height for 8-pixel wide fonts)
} __attribute__((packed));

static constexpr uint8_t PSF1_MAGIC0 = 0x36;
static constexpr uint8_t PSF1_MAGIC1 = 0x04;
static constexpr uint8_t PSF1_MODE512 = 0x01;

struct Psf2Header {
    uint32_t magic;           // 0x864AB572
    uint32_t version;         // 0
    uint32_t headersize;      // offset to glyph data
    uint32_t flags;           // 1 = has unicode table
    uint32_t numglyph;        // number of glyphs
    uint32_t bytesperglyph;   // bytes per glyph
    uint32_t height;          // glyph height in pixels
    uint32_t width;           // glyph width in pixels
} __attribute__((packed));

static constexpr uint32_t PSF2_MAGIC = 0x864AB572;

struct Font {
    const uint8_t* glyphs;    // pointer to first glyph bitmap
    uint32_t       numglyph;  // number of glyphs
    uint32_t       bytesperglyph;
    uint32_t       width;     // pixels
    uint32_t       height;    // pixels
};

inline bool parse(const uint8_t* data, Font* out) {
    const auto* h2 = reinterpret_cast<const Psf2Header*>(data);
    if (h2->magic == PSF2_MAGIC) {
        out->glyphs       = data + h2->headersize;
        out->numglyph     = h2->numglyph;
        out->bytesperglyph = h2->bytesperglyph;
        out->width         = h2->width;
        out->height        = h2->height;
        return true;
    }

    const auto* h1 = reinterpret_cast<const Psf1Header*>(data);
    if (h1->magic[0] == PSF1_MAGIC0 && h1->magic[1] == PSF1_MAGIC1) {
        out->glyphs       = data + sizeof(Psf1Header);
        out->numglyph     = (h1->mode & PSF1_MODE512) ? 512 : 256;
        out->bytesperglyph = h1->charsize;
        out->width         = 8;  // PSF1 is always 8 pixels wide
        out->height        = h1->charsize;
        return true;
    }

    return false;
}

inline const uint8_t* glyph(const Font* font, int ch) {
    if (ch < 0 || (uint32_t)ch >= font->numglyph)
        ch = '?';
    return font->glyphs + static_cast<size_t>(ch) * font->bytesperglyph;
}

} // namespace psf
