#pragma once

#include <base/types.h>

static inline void* memset(void *s, char c, size_t n) {
    char* p = (char*)s;
    while (n-- > 0) {
        *p++ = c;
    }
    return s;
}

static inline void* memcpy(void *dst, const void *src, size_t n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

inline void* operator new(__SIZE_TYPE__ size, void* ptr) noexcept {
    (void)size;
    return ptr;
}
