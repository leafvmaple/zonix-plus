#pragma once

//
// PSF (PC Screen Font) parser — supports PSF1 and PSF2 bitmap fonts.
//
// Usage:
//   extern const uint8_t _binary_fonts_console_psf_start[];
//   extern const uint8_t _binary_fonts_console_psf_end[];
//
//   psf::Font font;
//   psf::parse(_binary_fonts_console_psf_start, &font);
//   const uint8_t *glyph = psf::glyph(&font, 'A');
//

#include <base/types.h>

namespace psf {

// ---- PSF1 on-disk header (4 bytes) ----
struct Psf1Header {
    uint8_t  magic[2];    // 0x36, 0x04
    uint8_t  mode;        // 0=256, 1=512, 2=256+unicode, 3=512+unicode
    uint8_t  charsize;    // bytes per glyph (= height for 8-pixel wide fonts)
} __attribute__((packed));

static constexpr uint8_t PSF1_MAGIC0 = 0x36;
static constexpr uint8_t PSF1_MAGIC1 = 0x04;
static constexpr uint8_t PSF1_MODE512 = 0x01;

// ---- PSF2 on-disk header (32 bytes) ----
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

// ---- Parsed font descriptor (format-independent) ----
struct Font {
    const uint8_t* glyphs;    // pointer to first glyph bitmap
    uint32_t       numglyph;  // number of glyphs
    uint32_t       bytesperglyph;
    uint32_t       width;     // pixels
    uint32_t       height;    // pixels
};

// Parse a PSF1 or PSF2 font from a raw byte buffer.
// Returns true on success.
inline bool parse(const uint8_t* data, Font* out) {
    // Try PSF2 first (more specific magic)
    auto* h2 = (const Psf2Header*)data;
    if (h2->magic == PSF2_MAGIC) {
        out->glyphs       = data + h2->headersize;
        out->numglyph     = h2->numglyph;
        out->bytesperglyph = h2->bytesperglyph;
        out->width         = h2->width;
        out->height        = h2->height;
        return true;
    }

    // Try PSF1
    auto* h1 = (const Psf1Header*)data;
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

// Get the bitmap data for a given character code.
// Returns the '?' glyph for out-of-range characters.
inline const uint8_t* glyph(const Font* font, int ch) {
    if (ch < 0 || (uint32_t)ch >= font->numglyph)
        ch = '?';
    return font->glyphs + ch * font->bytesperglyph;
}

} // namespace psf
