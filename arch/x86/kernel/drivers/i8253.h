#pragma once

#include <base/types.h>

namespace i8253 {

int init();

}  // namespace i8253

namespace timer {

extern volatile int64_t ticks;

}  // namespace timer