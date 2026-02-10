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

static inline char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

static inline int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

static inline const char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == static_cast<char>(c)) return s;
        s++;
    }
    return nullptr;
}