#pragma once

#include <base/types.h>

// Simple string functions
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline const char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return s;
        s++;
    }
    return 0;
}