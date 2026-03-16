#pragma once

#include <base/types.h>

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

namespace i8253 {

int init();

}  // namespace i8253

namespace timer {

extern volatile int64_t ticks;

}  // namespace timer