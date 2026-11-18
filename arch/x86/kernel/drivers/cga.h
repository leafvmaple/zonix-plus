#pragma once
#include <abi/terminal.h>

#include <kernel/config.h>

namespace cga {

int init();
void putc(int c);
void scrup();
TerminalSize size();
void move_cursor(unsigned int row, unsigned int col);
void move_column(unsigned int col);
void clear();
void erase_line(unsigned int mode);
void set_cursor_visible(bool visible);

}  // namespace cga