#pragma once

#include <kernel/config.h>

namespace cga {

#ifdef CONFIG_CGA
void init();
void putc(int c);
void scrup();
#endif

} // namespace cga