#include "../command.h"
#include "grid_renderer.h"
#include <kernel/psf.h>

extern "C" const unsigned char terminal_font[];

namespace {

using namespace desktop_ipc;

constexpr int FIRST_GLYPH = ' ';
constexpr int ATLAS_COLS = 16;
constexpr int ATLAS_ROWS = 6;
constexpr int ATLAS_WIDTH = ATLAS_COLS * GLYPH_WIDTH;
constexpr int ATLAS_HEIGHT = ATLAS_ROWS * GLYPH_HEIGHT;
constexpr int CURSOR_GLYPH = ATLAS_ROWS * ATLAS_COLS - 1;
constexpr zocos::Color4B TEXT_COLOR{241, 245, 249, 255};
constexpr zocos::Color4B CURSOR_COLOR{114, 207, 204, 255};
constexpr zocos::Color4B BACKGROUND{17, 29, 41, 255};
static_assert(FIRST_GLYPH + CURSOR_GLYPH == '~' + 1);

void add_glyph(zocos::RenderCommand& batch, int glyph, int x, int y, int height, zocos::Color4B color) {
    float u0 = static_cast<float>(glyph % ATLAS_COLS) / ATLAS_COLS;
    float u1 = static_cast<float>(glyph % ATLAS_COLS + 1) / ATLAS_COLS;
    float v0 = 1.f - static_cast<float>(glyph / ATLAS_COLS) / ATLAS_ROWS;
    float v1 = 1.f - static_cast<float>(glyph / ATLAS_COLS + 1) / ATLAS_ROWS;
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
    psf::Font font{};
    if (!psf::parse(terminal_font, &font) || font.width != GLYPH_WIDTH || font.height != GLYPH_HEIGHT)
        return SYS_ERROR_NOT_SUPPORTED;

    sys::vector<unsigned char> atlas(ATLAS_WIDTH * ATLAS_HEIGHT);
    for (int slot = 0; slot <= CURSOR_GLYPH; ++slot) {
        const auto* bits = psf::glyph(&font, FIRST_GLYPH + slot);
        int left = slot % ATLAS_COLS * GLYPH_WIDTH;
        int top = slot / ATLAS_COLS * GLYPH_HEIGHT;
        for (int y = 0; y < GLYPH_HEIGHT; ++y)
            for (int x = 0; x < GLYPH_WIDTH; ++x)
                atlas[(top + y) * ATLAS_WIDTH + left + x] =
                    slot == CURSOR_GLYPH || (bits[y] & (0x80U >> x)) != 0 ? 255 : 0;
    }
    glyphs_.texture = device_.createTexture({ATLAS_WIDTH, ATLAS_HEIGHT, zocos::TextureFormat::A8Unorm, {atlas.data()}});
    if (!glyphs_.texture.isValid())
        return SYS_ERROR_IO;

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
                add_glyph(glyphs_, c - FIRST_GLYPH, col * GLYPH_WIDTH - x, row * GLYPH_HEIGHT - y, GLYPH_HEIGHT,
                          TEXT_COLOR);
        }
    }
    int cursor_x = cells[CURSOR_COL] * GLYPH_WIDTH - x;
    int cursor_y = cells[CURSOR_ROW] * GLYPH_HEIGHT - y;
    if (cells[CURSOR_VISIBLE] && cursor_x >= 0 && cursor_x < width && cursor_y >= 0 && cursor_y < height)
        add_glyph(glyphs_, CURSOR_GLYPH, cursor_x, cursor_y + GLYPH_HEIGHT - 2, 2, CURSOR_COLOR);

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
