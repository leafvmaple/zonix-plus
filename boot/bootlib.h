// Shared boot utility functions for BIOS and UEFI bootloaders
// These are common memory/string operations used by both bootloaders.
// Defined as static inline to avoid cross-compilation linkage issues
// (BIOS is 32-bit, UEFI is 64-bit).

#ifndef _BOOT_BOOTLIB_H_
#define _BOOT_BOOTLIB_H_

#include <base/types.h>

static inline void* memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static inline void* memset(void* dst, int c, size_t n) {
    char* d = (char*)dst;
    while (n--)
        *d++ = (char)c;
    return dst;
}

static inline int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

static inline void* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

#endif /* _BOOT_BOOTLIB_H_ */
