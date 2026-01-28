#pragma once

void __panic(const char *file, int line, const char *fmt, ...);

#define panic(...) \
    __panic(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)                              \
    do {                                       \
        if (!(x)) {                            \
            panic("assertion failed: %s", #x); \
        }                                      \
    } while (0)
