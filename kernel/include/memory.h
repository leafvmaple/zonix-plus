#pragma once

#include <base/types.h>

static inline void* memset(void* s, char c, size_t n) {
    auto* p = static_cast<char*>(s);
    while (n-- > 0) {
        *p++ = c;
    }
    return s;
}

static inline void* memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<char*>(dst);
    const auto* s = static_cast<const char*>(src);
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

static inline int memcmp(const void* s1, const void* s2, size_t n) {
    const auto* p1 = static_cast<const unsigned char*>(s1);
    const auto* p2 = static_cast<const unsigned char*>(s2);
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

// Forward declarations for kernel memory allocation
void* kmalloc(size_t size);
void kfree(void* ptr);

// Global operator new - uses page-aligned kmalloc
// Memory is always page-aligned (4KB) since kmalloc uses page allocator
inline void* operator new(__SIZE_TYPE__ size) noexcept {
    return kmalloc(size);
}

inline void* operator new[](__SIZE_TYPE__ size) noexcept {
    return kmalloc(size);
}

// Global operator delete - uses kfree
inline void operator delete(void* ptr) noexcept {
    kfree(ptr);
}

inline void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}

// Sized delete variants (C++14)
inline void operator delete(void* ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}

inline void operator delete[](void* ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}
