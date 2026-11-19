#include "../command.h"
#include "grid_renderer.h"
#include "../gui/platform.h"
#include "../gui/theme.h"

namespace {

using namespace desktop_ipc;
using zonix_gui::MonoFont;

constexpr zocos::Color4B TEXT_COLOR = zonix_gui::opaque_color(zonix_gui::theme::TEXT);
constexpr zocos::Color4B CURSOR_COLOR = zonix_gui::opaque_color(zonix_gui::theme::ACCENT);
constexpr zocos::Color4B BACKGROUND = zonix_gui::opaque_color(zonix_gui::theme::WINDOW_BACKGROUND);

// The frame is drawn with a top-left origin.
void add_glyph(zocos::RenderCommand& batch, zocos::Rect cell, int x, int y, int height, zocos::Color4B color) {
    float u0 = cell.x;
    float u1 = cell.x + cell.width;
    float v0 = cell.y + cell.height;
    float v1 = cell.y;
    float left = static_cast<float>(x);
    float right = static_cast<float>(x + GLYPH_WIDTH);
    float top = static_cast<float>(y);
    float bottom = static_cast<float>(y + height);

    zocos::QuadVertex tl{{left, top}, {u0, v0}, color};
    zocos::QuadVertex tr{{right, top}, {u1, v0}, color};
    zocos::QuadVertex bl{{left, bottom}, {u0, v1}, color};
    zocos::QuadVertex br{{right, bottom}, {u1, v1}, color};
    batch.vertices.push_back(tl);
    batch.vertices.push_back(bl);
    batch.vertices.push_back(br);
    batch.vertices.push_back(tl);
    batch.vertices.push_back(br);
    batch.vertices.push_back(tr);
}

}  // namespace

long GridRenderer::init() {
    long error = font_.init(device_);
    if (error < 0)
        return error;
    glyphs_.texture = font_.texture();

    device_.setClearColor(BACKGROUND);
    pixels_.resize(TERMINAL_WIDTH * TERMINAL_HEIGHT * 4);
    glyphs_.vertices.reserve((TERMINAL_COLS * TERMINAL_ROWS + 1) * 6);
    return zonix_gui::WindowChannel::open(TERMINAL_WIDTH, TERMINAL_HEIGHT);
}

long GridRenderer::publish(const Frame& frame) {
    auto damage = compare_text(previous_, frame);
    if (damage.empty())
        return 0;

    int x = damage.left * GLYPH_WIDTH;
    int y = damage.top * GLYPH_HEIGHT;
    int width = (damage.right - damage.left) * GLYPH_WIDTH;
    int height = (damage.bottom - damage.top) * GLYPH_HEIGHT;
    constexpr sys::size_t pitch = TERMINAL_WIDTH * 4;
    sys::size_t offset = y * pitch + x * 4;
    if (!device_.setOutputBuffer({pixels_.data() + offset, width, height, pitch, pixels_.size() - offset}))
        return SYS_ERROR_IO;

    const auto* cells = reinterpret_cast<const unsigned char*>(frame.data());
    glyphs_.vertices.clear();
    for (int row = damage.top; row < damage.bottom; ++row) {
        for (int col = damage.left; col < damage.right; ++col) {
            int c = cells[row * TERMINAL_COLS + col];
            if (c != ' ')
                add_glyph(glyphs_, MonoFont::cell(c), col * GLYPH_WIDTH - x, row * GLYPH_HEIGHT - y, GLYPH_HEIGHT,
                          TEXT_COLOR);
        }
    }
    int cursor_x = cells[CURSOR_COL] * GLYPH_WIDTH - x;
    int cursor_y = cells[CURSOR_ROW] * GLYPH_HEIGHT - y;
    if (cells[CURSOR_VISIBLE] && cursor_x >= 0 && cursor_x < width && cursor_y >= 0 && cursor_y < height)
        add_glyph(glyphs_, MonoFont::solid(), cursor_x, cursor_y + GLYPH_HEIGHT - 2, 2, CURSOR_COLOR);

    device_.beginFrame(zocos::Mat4::ortho(0.f, width, height, 0.f, -1.f, 1.f), width, height);
    device_.submit(glyphs_);
    device_.endFrame();

    auto result = channel_.present({pixels_.data(), TERMINAL_WIDTH, TERMINAL_HEIGHT, pitch, pixels_.size()},
                                   {static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                                    static_cast<unsigned int>(width), static_cast<unsigned int>(height)});
    if (result == 0)
        previous_ = frame;
    return result;
}
