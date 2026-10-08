#pragma once

void panic_at(const char* file, int line, const char* fmt, ...);

#define PANIC(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)                              \
    do {                                       \
        if (!(x)) {                            \
            PANIC("assertion failed: %s", #x); \
        }                                      \
    } while (0)
