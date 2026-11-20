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

long GridRenderer::resize(int cols, int rows) {
    if (!font_ready_) {
        long error = font_.init(device_);
        if (error < 0)
            return error;
        glyphs_.texture = font_.texture();
        device_.setClearColor(BACKGROUND);
        font_ready_ = true;
    }
    long error = zonix_gui::WindowChannel::open(static_cast<unsigned int>(cols * GLYPH_WIDTH),
                                                static_cast<unsigned int>(rows * GLYPH_HEIGHT));
    if (error < 0)
        return error;
    cols_ = cols;
    rows_ = rows;
    pixels_.resize(static_cast<sys::size_t>(cols) * GLYPH_WIDTH * rows * GLYPH_HEIGHT * 4);
    glyphs_.vertices.reserve((static_cast<sys::size_t>(cols) * rows + 1) * 6);
    shown_.cells.resize(static_cast<sys::size_t>(cols) * rows);
    everything_ = true;
    return 0;
}

long GridRenderer::publish(const terminal_grid::Grid& grid) {
    if (grid.cols() != cols_ || grid.rows() != rows_)
        return SYS_ERROR_INVALID;
    // The changed cells, as a rectangle of cells. The cursor's old and new
    // cells count as changed when it moves or blinks on or off.
    int left = cols_, top = rows_, right = 0, bottom = 0;
    auto include = [&](int row, int col) {
        left = sys::min(left, col);
        top = sys::min(top, row);
        right = sys::max(right, col + 1);
        bottom = sys::max(bottom, row + 1);
    };
    if (everything_) {
        include(0, 0);
        include(rows_ - 1, cols_ - 1);
    } else {
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col) {
                if (shown_.cells[row * cols_ + col] != grid.at(row, col))
                    include(row, col);
            }
        }
        if (shown_.cursor != grid.cursor_visible() || shown_.row != grid.row() || shown_.col != grid.col()) {
            if (shown_.cursor)
                include(shown_.row, sys::min(shown_.col, cols_ - 1));
            if (grid.cursor_visible())
                include(grid.row(), grid.col());
        }
    }
    if (right <= left)
        return 0;

    int x = left * GLYPH_WIDTH;
    int y = top * GLYPH_HEIGHT;
    int width = (right - left) * GLYPH_WIDTH;
    int height = (bottom - top) * GLYPH_HEIGHT;
    const sys::size_t pitch = static_cast<sys::size_t>(cols_) * GLYPH_WIDTH * 4;
    sys::size_t offset = y * pitch + x * 4;
    if (!device_.setOutputBuffer({pixels_.data() + offset, width, height, pitch, pixels_.size() - offset}))
        return SYS_ERROR_IO;

    glyphs_.vertices.clear();
    for (int row = top; row < bottom; ++row) {
        for (int col = left; col < right; ++col) {
            int c = grid.at(row, col);
            if (c != ' ')
                add_glyph(glyphs_, MonoFont::cell(c), col * GLYPH_WIDTH - x, row * GLYPH_HEIGHT - y, GLYPH_HEIGHT,
                          TEXT_COLOR);
        }
    }
    int cursor_x = grid.col() * GLYPH_WIDTH - x;
    int cursor_y = grid.row() * GLYPH_HEIGHT - y;
    if (grid.cursor_visible() && cursor_x >= 0 && cursor_x < width && cursor_y >= 0 && cursor_y < height)
        add_glyph(glyphs_, MonoFont::solid(), cursor_x, cursor_y + GLYPH_HEIGHT - 2, 2, CURSOR_COLOR);

    device_.beginFrame(zocos::Mat4::ortho(0.f, width, height, 0.f, -1.f, 1.f), width, height);
    device_.submit(glyphs_);
    device_.endFrame();

    auto result = channel_.present({pixels_.data(), cols_ * GLYPH_WIDTH, rows_ * GLYPH_HEIGHT, pitch, pixels_.size()},
                                   {static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                                    static_cast<unsigned int>(width), static_cast<unsigned int>(height)});
    if (result == 0) {
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col)
                shown_.cells[row * cols_ + col] = grid.at(row, col);
        }
        shown_.row = grid.row();
        shown_.col = grid.col();
        shown_.cursor = grid.cursor_visible();
        everything_ = false;
    }
    return result;
}
