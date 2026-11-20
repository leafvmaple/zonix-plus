#pragma once

#include "../desktop/text_frame.h"
#include <base/terminal_parser.h>
#include <sys/algorithm.hpp>
#include <sys/vector.hpp>

namespace terminal_grid {

// The character cells of the terminal and its cursor. The terminal parser
// writes into it, and the window shows it. Its size follows the window.
class Grid {
public:
    static constexpr int MIN_COLS = 20;
    static constexpr int MIN_ROWS = 4;

    Grid(int cols, int rows) {
        resize(cols, rows);
        cursor(true);
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int row() const { return row_; }
    int col() const { return col_; }
    bool cursor_visible() const { return cursor_visible_; }
    unsigned char at(int row, int col) const { return cells_[row * cols_ + col]; }

    // Keeps the text at the top left. When the grid gets shorter than the
    // cursor's line, the lines above scroll off so the cursor stays visible.
    void resize(int cols, int rows) {
        int shift = row_ >= rows ? row_ - rows + 1 : 0;
        sys::vector<unsigned char> next;
        next.resize(static_cast<sys::size_t>(cols) * rows, ' ');
        for (int line = 0; line < rows && line + shift < rows_; ++line) {
            for (int column = 0; column < cols && column < cols_; ++column)
                next[line * cols + column] = cells_[(line + shift) * cols_ + column];
        }
        cells_ = sys::move(next);
        cols_ = cols;
        rows_ = rows;
        row_ -= shift;
        col_ = sys::min(col_, cols - 1);
    }

    void feed(const char* bytes, unsigned int count) {
        for (unsigned int i = 0; i < count; ++i)
            parser_.putc(static_cast<unsigned char>(bytes[i]), *this);
    }

    // The parser's callbacks.
    void text(int c) {
        switch (c) {
            case '\r': col_ = 0; break;
            case '\n': newline(); break;
            case '\b':
            case 127:
                if (col_ > 0)
                    --col_;
                break;
            case '\t': {
                int stop = (col_ / 8 + 1) * 8;
                for (int i = col_; i < stop; ++i)
                    text(' ');
                break;
            }
            default:
                if (c >= 32) {
                    cells_[row_ * cols_ + col_] = static_cast<unsigned char>(c <= 126 ? c : '?');
                    if (++col_ == cols_)
                        newline();
                }
                break;
        }
    }
    void cursor(bool visible) { cursor_visible_ = visible; }
    void position(unsigned int row, unsigned int col) {
        row_ = row < static_cast<unsigned int>(rows_) ? static_cast<int>(row) : rows_ - 1;
        column(col);
    }
    void column(unsigned int col) { col_ = col < static_cast<unsigned int>(cols_) ? static_cast<int>(col) : cols_ - 1; }
    void erase_line(unsigned int mode) {
        int first = mode == 2 ? 0 : col_;
        for (int i = first; i < cols_; ++i)
            cells_[row_ * cols_ + i] = ' ';
    }
    void clear() {
        for (auto& cell : cells_)
            cell = ' ';
        row_ = col_ = 0;
    }

private:
    sys::vector<unsigned char> cells_;
    terminal::Parser parser_{};
    int cols_{};
    int rows_{};
    int row_{};
    int col_{};
    bool cursor_visible_{};

    void newline() {
        col_ = 0;
        if (row_ < rows_ - 1) {
            ++row_;
            return;
        }
        for (int i = 0; i < cols_ * (rows_ - 1); ++i)
            cells_[i] = cells_[i + cols_];
        erase_line(2);
    }
};

}  // namespace terminal_grid
