#pragma once

#include <base/types.h>

namespace fbcons {

void late_init();
void init(uintptr_t fb_vaddr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp);
void putc(int c);
void tick();
bool is_active();

}  // namespace fbcons
