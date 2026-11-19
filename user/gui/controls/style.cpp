#include "style.h"
#include "../platform.h"
#include "../theme.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>

namespace zonix_gui::control_style {
namespace {

constexpr zocos::Color4B rgb(unsigned int value, unsigned char alpha = 255) {
    return {static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 8),
            static_cast<unsigned char>(value), alpha};
}

// Indexed by Color.
constexpr zocos::Color4B COLORS[] = {
    rgb(theme::WINDOW_BACKGROUND),      // Window
    rgb(0x1A2A39),                      // Panel
    rgb(0x152230),                      // Sidebar
    rgb(theme::WINDOW_BORDER),          // Border
    rgb(0x263746),                      // Divider
    rgb(0x2A3F52),                      // Control
    rgb(0x3B6A86),                      // Pressed
    rgb(0x1E2D3B),                      // Disabled
    rgb(0x0E1822),                      // Field
    rgb(0x26506A),                      // Selected
    rgb(theme::ACCENT),                 // SelectedBorder
    rgb(0x2E8C8A),                      // Accent
    rgb(0x3FA9A6),                      // AccentPressed
    rgb(theme::CLOSE),                  // Danger
    rgb(0xE88791),                      // DangerPressed
    rgb(theme::TEXT),                   // Text
    rgb(theme::TEXT, 97),               // TextDisabled
    rgb(theme::TITLE_BACKGROUND),       // Title
    rgb(theme::TITLE_DIVIDER),          // TitleDivider
    rgb(0xEFBC68),                      // Folder
    rgb(0xD39A4A),                      // FolderShade
    rgb(0xB77E3A),                      // FolderTab
    rgb(0xD6E2EB),                      // File
    rgb(0x86A0B2),                      // FileShade
    rgb(0xA6BCCB),                      // FileFold
    rgb(0x000000, 84),                  // Shadow
    rgb(0x060A0F, 150),                 // Scrim
};
constexpr int COLOR_COUNT = static_cast<int>(Color::Scrim) + 1;
static_assert(sizeof(COLORS) / sizeof(*COLORS) == COLOR_COUNT);

constexpr int SAMPLES = 4;
constexpr int GLYPH_PIXELS = static_cast<int>(GLYPH_SIZE);
constexpr float STROKE = 0.85f;
constexpr zocos::Rect SOLID_UV{0.5f, 0.5f, 0.f, 0.f};

// Glyph strokes in a GLYPH_SIZE square with a top-left origin.
struct Segment {
    float ax;
    float ay;
    float bx;
    float by;
};
constexpr Segment STROKES[][2] = {
    {{2.5f, 2.5f, 9.5f, 9.5f}, {9.5f, 2.5f, 2.5f, 9.5f}},        // Close
    {{7.75f, 2.5f, 4.25f, 6.f}, {4.25f, 6.f, 7.75f, 9.5f}},      // Back
    {{4.25f, 2.5f, 7.75f, 6.f}, {7.75f, 6.f, 4.25f, 9.5f}},      // Forward
    {{2.5f, 7.75f, 6.f, 4.25f}, {6.f, 4.25f, 9.5f, 7.75f}},      // Up
};
static_assert(sizeof(STROKES) / sizeof(*STROKES) == static_cast<int>(Glyph::Up) + 1);

bool on_stroke(float x, float y, const Segment& segment) {
    float dx = segment.bx - segment.ax;
    float dy = segment.by - segment.ay;
    float along = ((x - segment.ax) * dx + (y - segment.ay) * dy) / (dx * dx + dy * dy);
    along = mstd::clamp(along, 0.f, 1.f);
    float off_x = x - (segment.ax + along * dx);
    float off_y = y - (segment.ay + along * dy);
    return off_x * off_x + off_y * off_y <= STROKE * STROKE;
}

float snap(float value) {
    return static_cast<float>(static_cast<int>(value + 0.5f));
}

using Vertices = mstd::vector<zocos::QuadVertex>;

void push_quad(Vertices& out, zocos::Rect area, zocos::Rect uv, zocos::Color4B color) {
    if (area.width <= 0.f || area.height <= 0.f)
        return;
    const float x0 = area.x;
    const float y0 = area.y;
    const float x1 = area.x + area.width;
    const float y1 = area.y + area.height;
    const float u0 = uv.x;
    const float v0 = uv.y;
    const float u1 = uv.x + uv.width;
    const float v1 = uv.y + uv.height;
    out.push_back({{x0, y0}, {u0, v0}, color});
    out.push_back({{x1, y0}, {u1, v0}, color});
    out.push_back({{x1, y1}, {u1, v1}, color});
    out.push_back({{x0, y0}, {u0, v0}, color});
    out.push_back({{x1, y1}, {u1, v1}, color});
    out.push_back({{x0, y1}, {u0, v1}, color});
}

zocos::Color4B value(Color color) {
    return COLORS[static_cast<int>(color)];
}

}  // namespace

zocos::Label* make_label(zocos::Director& director, const mstd::string& text) {
    return zocos::Label::createWithTTF(director, text, UI_FONT_PATH, FONT_SIZE);
}

void place_label(zocos::Label& label, float x, float center_y, float align, float max_width) {
    label.updateLayout();
    const auto size = label.getContentSize();
    float scale = max_width > 0.f && size.width > max_width ? max_width / size.width : 1.f;
    label.setScale(scale);
    label.setAnchorPoint({0.f, 0.f});
    label.setPosition({snap(x - align * size.width * scale), snap(center_y - size.height * scale / 2.f)});
}

Palette::~Palette() {
    auto* device = director_.getRenderDevice();
    if (!device)
        return;
    if (solid_.isValid())
        device->destroyTexture(solid_);
    for (auto texture : discs_) {
        if (texture.isValid())
            device->destroyTexture(texture);
    }
    for (auto texture : glyphs_) {
        if (texture.isValid())
            device->destroyTexture(texture);
    }
}

bool Palette::init() {
    auto* device = director_.getRenderDevice();
    if (!device)
        return false;
    const unsigned char opaque = 255;
    solid_ = device->createTexture({1, 1, zocos::TextureFormat::A8Unorm, {&opaque}});
    if (!solid_.isValid())
        return false;

    // Coverage comes from SAMPLES x SAMPLES points per texel. Positions are in
    // units of 1 / (2 * SAMPLES) of a pixel so the sample centres are integers.
    constexpr int unit = SAMPLES * 2;
    unsigned char alpha[MAX_RADIUS * 2 * MAX_RADIUS * 2];
    for (int radius = 1; radius <= MAX_RADIUS; ++radius) {
        const int size = radius * 2;
        const int reach = radius * unit;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int inside = 0;
                for (int sample = 0; sample < SAMPLES * SAMPLES; ++sample) {
                    int dx = x * unit + sample % SAMPLES * 2 + 1 - reach;
                    int dy = y * unit + sample / SAMPLES * 2 + 1 - reach;
                    if (dx * dx + dy * dy <= reach * reach)
                        ++inside;
                }
                alpha[y * size + x] = static_cast<unsigned char>(inside * 255 / (SAMPLES * SAMPLES));
            }
        }
        discs_[radius - 1] = device->createTexture({size, size, zocos::TextureFormat::A8Unorm, {alpha}});
        if (!discs_[radius - 1].isValid())
            return false;
    }

    static_assert(GLYPH_PIXELS * GLYPH_PIXELS <= static_cast<int>(sizeof(alpha)));
    for (int index = 0; index < GLYPH_COUNT; ++index) {
        for (int y = 0; y < GLYPH_PIXELS; ++y) {
            for (int x = 0; x < GLYPH_PIXELS; ++x) {
                int inside = 0;
                for (int sample = 0; sample < SAMPLES * SAMPLES; ++sample) {
                    float sample_x = x + (sample % SAMPLES * 2 + 1) / static_cast<float>(unit);
                    float sample_y = y + (sample / SAMPLES * 2 + 1) / static_cast<float>(unit);
                    if (on_stroke(sample_x, sample_y, STROKES[index][0]) ||
                        on_stroke(sample_x, sample_y, STROKES[index][1]))
                        ++inside;
                }
                alpha[y * GLYPH_PIXELS + x] = static_cast<unsigned char>(inside * 255 / (SAMPLES * SAMPLES));
            }
        }
        glyphs_[index] =
            device->createTexture({GLYPH_PIXELS, GLYPH_PIXELS, zocos::TextureFormat::A8Unorm, {alpha}});
        if (!glyphs_[index].isValid())
            return false;
    }
    return true;
}

void Palette::box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Color color,
                  zocos::RenderSortKey order) const {
    Vertices quads;
    push_quad(quads, area, SOLID_UV, value(color));
    renderer.addDrawQuads(world, solid_, quads, 1.f, order);
}

void Palette::round_box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, float radius,
                        Color color, zocos::RenderSortKey order) const {
    int corner = static_cast<int>(mstd::min(radius, mstd::min(area.width, area.height) / 2.f));
    corner = mstd::min(corner, MAX_RADIUS);
    if (corner < 1) {
        box(renderer, world, area, color, order);
        return;
    }
    const auto tint = value(color);
    const float r = static_cast<float>(corner);
    const float left = area.x;
    const float bottom = area.y;
    const float right = area.x + area.width - r;
    const float top = area.y + area.height - r;

    Vertices sides;
    push_quad(sides, {left, bottom + r, area.width, area.height - 2.f * r}, SOLID_UV, tint);
    push_quad(sides, {left + r, bottom, area.width - 2.f * r, r}, SOLID_UV, tint);
    push_quad(sides, {left + r, top, area.width - 2.f * r, r}, SOLID_UV, tint);
    renderer.addDrawQuads(world, solid_, sides, 1.f, order);

    // Each corner shows the matching quarter of the disc, one texel per pixel.
    Vertices corners;
    push_quad(corners, {left, bottom, r, r}, {0.f, 0.f, 0.5f, 0.5f}, tint);
    push_quad(corners, {right, bottom, r, r}, {0.5f, 0.f, 0.5f, 0.5f}, tint);
    push_quad(corners, {left, top, r, r}, {0.f, 0.5f, 0.5f, 0.5f}, tint);
    push_quad(corners, {right, top, r, r}, {0.5f, 0.5f, 0.5f, 0.5f}, tint);
    renderer.addDrawQuads(world, discs_[corner - 1], corners, 1.f, order);
}

void Palette::frame(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, float radius, Color fill,
                    Color border, zocos::RenderSortKey order) const {
    round_box(renderer, world, area, radius, border, order);
    round_box(renderer, world, {area.x + 1.f, area.y + 1.f, area.width - 2.f, area.height - 2.f}, radius - 1.f, fill,
              order);
}

void Palette::button(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Size size, Color color,
                     zocos::RenderSortKey order) const {
    frame(renderer, world, {0.f, 0.f, size.width, size.height}, CONTROL_RADIUS, color, Color::Border, order);
}

void Palette::glyph(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Glyph symbol, Color color,
                    zocos::RenderSortKey order) const {
    Vertices quads;
    push_quad(quads,
              {area.x + snap((area.width - GLYPH_SIZE) / 2.f), area.y + snap((area.height - GLYPH_SIZE) / 2.f),
               GLYPH_SIZE, GLYPH_SIZE},
              {0.f, 0.f, 1.f, 1.f}, value(color));
    renderer.addDrawQuads(world, glyphs_[static_cast<int>(symbol)], quads, 1.f, order);
}

}  // namespace zonix_gui::control_style
