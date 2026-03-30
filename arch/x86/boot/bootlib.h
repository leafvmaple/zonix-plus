// Shared boot utility functions for BIOS and UEFI bootloaders
// These are common memory/string operations used by both bootloaders.
// Defined as static inline to avoid cross-compilation linkage issues
// (BIOS is 32-bit, UEFI is 64-bit).

#pragma once

#include <base/types.h>

static inline void* memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<char*>(dst);
    auto* s = static_cast<const char*>(src);
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static inline void* memset(void* dst, int c, size_t n) {
    auto* d = static_cast<char*>(dst);
    while (n--)
        *d++ = static_cast<char>(c);
    return dst;
}

static inline int memcmp(const void* s1, const void* s2, size_t n) {
    auto* p1 = static_cast<const uint8_t*>(s1);
    auto* p2 = static_cast<const uint8_t*>(s2);
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

static inline void* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}
