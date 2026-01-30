#pragma once

#include <base/types.h>

// Simple string functions
static inline size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i++] = '\0';
    }
    return dst;
}

static inline const char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == static_cast<char>(c)) return s;
        s++;
    }
    return nullptr;
}