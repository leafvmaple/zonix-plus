#pragma once

#include <kernel/config.h>

#ifdef CONFIG_SERIAL

namespace serial {

void init();
void putc(int c);

} // namespace serial

#endif // CONFIG_SERIAL
