#pragma once

#include "grid.h"
#include "../gui/mono_font.h"
#include "../gui/window_channel.h"

// Draws the grid's changed cells in the console font and sends them to
// Desktop as part of the window's surface.
class GridRenderer {
public:
    // Sizes the window's surface for a grid, asking Desktop for it. The next
    // publish draws every cell.
    long resize(int cols, int rows);
    long publish(const terminal_grid::Grid& grid);

private:
    // The cells, cursor and size the window shows.
    struct Shown {
        sys::vector<unsigned char> cells;
        int row{};
        int col{};
        bool cursor{};
    };

    zocos::SoftwareRenderDevice device_;
    zonix_gui::MonoFont font_;
    bool font_ready_{};
    zocos::RenderCommand glyphs_;
    zonix_gui::WindowChannel channel_;
    sys::vector<unsigned char> pixels_;
    int cols_{};
    int rows_{};
    Shown shown_;
    bool everything_{};
};
