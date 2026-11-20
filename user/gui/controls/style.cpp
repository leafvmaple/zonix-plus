#include "style.h"
#include "../platform.h"
#include "../shape.h"
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
    rgb(theme::TITLE_BACKGROUND),       // Panel
    rgb(0x252527),                      // Sidebar
    rgb(0x4A4A4E),                      // Border
    rgb(0x3A3A3D),                      // Divider
    rgb(0x48484C),                      // Control
    rgb(0x5E5E63),                      // Pressed
    rgb(0x333336),                      // Disabled
    rgb(0x1A1A1C),                      // Field
    rgb(theme::SELECTION),              // Selected
    rgb(theme::ACCENT),                 // SelectedBorder
    rgb(theme::ACCENT),                 // Accent
    rgb(0x409CFF),                      // AccentPressed
    rgb(0xE5484D),                      // Danger
    rgb(0xF2686C),                      // DangerPressed
    rgb(theme::TEXT),                   // Text
    rgb(theme::TEXT, 97),               // TextDisabled
    rgb(theme::CLOSE),                  // Close
    rgb(theme::FOLDER),                 // Folder
    rgb(0x3F97E3),                      // FolderShade
    rgb(0x2F82CF),                      // FolderTab
    rgb(0xE4E4E8),                      // File
    rgb(0x9A9AA2),                      // FileShade
    rgb(0xBDBDC5),                      // FileFold
    rgb(0x000000, 96),                  // Shadow
    rgb(0x000000, 120),                 // Scrim
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
    return zocos::Label::createWithTTF(director, text, UI_FONT_PATH, TextSize::get());
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
    if (attached_ && --users_ == 0)
        destroy(director_.getRenderDevice());
}

bool Palette::init() {
    if (attached_)
        return true;
    auto* device = director_.getRenderDevice();
    if (!device)
        return false;
    if (users_ == 0 && !create(*device)) {
        destroy(device);
        return false;
    }
    ++users_;
    attached_ = true;
    return true;
}

// The device is gone once the director has shut down; its textures went with it.
void Palette::destroy(zocos::RenderDevice* device) {
    if (device) {
        if (textures_.solid.isValid())
            device->destroyTexture(textures_.solid);
        for (auto texture : textures_.discs) {
            if (texture.isValid())
                device->destroyTexture(texture);
        }
        for (auto texture : textures_.glyphs) {
            if (texture.isValid())
                device->destroyTexture(texture);
        }
    }
    textures_ = {};
}

bool Palette::create(zocos::RenderDevice& device_ref) {
    auto* device = &device_ref;
    const unsigned char opaque = 255;
    textures_.solid = device->createTexture({1, 1, zocos::TextureFormat::A8Unorm, {&opaque}});
    if (!textures_.solid.isValid())
        return false;

    constexpr int unit = SAMPLES * 2;
    unsigned char alpha[MAX_RADIUS * 2 * MAX_RADIUS * 2];
    for (int radius = 1; radius <= MAX_RADIUS; ++radius) {
        const int size = radius * 2;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x)
                alpha[y * size + x] = shape::disc_coverage(radius, x, y);
        }
        textures_.discs[radius - 1] = device->createTexture({size, size, zocos::TextureFormat::A8Unorm, {alpha}});
        if (!textures_.discs[radius - 1].isValid())
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
        textures_.glyphs[index] =
            device->createTexture({GLYPH_PIXELS, GLYPH_PIXELS, zocos::TextureFormat::A8Unorm, {alpha}});
        if (!textures_.glyphs[index].isValid())
            return false;
    }
    return true;
}

void Palette::box(zocos::Renderer& renderer, const zocos::Mat4& world, zocos::Rect area, Color color,
                  zocos::RenderSortKey order) const {
    Vertices quads;
    push_quad(quads, area, SOLID_UV, value(color));
    renderer.addDrawQuads(world, textures_.solid, quads, 1.f, order);
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
    renderer.addDrawQuads(world, textures_.solid, sides, 1.f, order);

    // Each corner shows the matching quarter of the disc, one texel per pixel.
    Vertices corners;
    push_quad(corners, {left, bottom, r, r}, {0.f, 0.f, 0.5f, 0.5f}, tint);
    push_quad(corners, {right, bottom, r, r}, {0.5f, 0.f, 0.5f, 0.5f}, tint);
    push_quad(corners, {left, top, r, r}, {0.f, 0.5f, 0.5f, 0.5f}, tint);
    push_quad(corners, {right, top, r, r}, {0.5f, 0.5f, 0.5f, 0.5f}, tint);
    renderer.addDrawQuads(world, textures_.discs[corner - 1], corners, 1.f, order);
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
    renderer.addDrawQuads(world, textures_.glyphs[static_cast<int>(symbol)], quads, 1.f, order);
}

}  // namespace zonix_gui::control_style
