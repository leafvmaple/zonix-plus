#pragma once

#include <kernel/config.h>

namespace uart8250 {

int init();
void putc(int c);

}  // namespace uart8250
