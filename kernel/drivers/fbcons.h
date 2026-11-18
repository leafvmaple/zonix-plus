#pragma once
#include <abi/terminal.h>

#include <base/types.h>

namespace fbcons {

void late_init();
void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp);
void putc(int c);
void tick();
bool is_active();
TerminalSize size();
void move_cursor(unsigned int row, unsigned int col);
void move_column(unsigned int col);
void clear();
void erase_line(unsigned int mode);
void set_cursor_visible(bool visible);
void suspend();
void resume();

}  // namespace fbcons
