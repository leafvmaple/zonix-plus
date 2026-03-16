#pragma once

#include <base/types.h>

namespace timer {

int init();
void set_next();
extern volatile int64_t ticks;

}  // namespace timer
