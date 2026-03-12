#pragma once

#include <base/types.h>
#include <asm/arch.h>

// memset/memcpy are provided as extern "C" symbols in kernel/cxxrt.cpp
// so that clang can resolve implicit calls (struct zeroing, assignment, etc.)
extern "C" void* memset(void* s, int c, size_t n);
extern "C" void* memcpy(void* dst, const void* src, size_t n);
extern "C" int memcmp(const void* s1, const void* s2, size_t n);

inline void* operator new(__SIZE_TYPE__ size, void* ptr) noexcept {
    (void)size;
    return ptr;
}

// Forward declarations for kernel memory allocation
void* kmalloc(size_t size);
void kfree(void* ptr);

namespace std {
// NOLINTNEXTLINE(readability-identifier-naming)
struct nothrow_t {
    explicit nothrow_t() = default;
};
// NOLINTNEXTLINE(readability-identifier-naming)
extern const nothrow_t nothrow;
}  // namespace std

// Memory is always page-aligned (4KB) since kmalloc uses page allocator
// Definitions are in kernel/cxxrt.cpp to avoid Clang's -Winline-new-delete
void* operator new(__SIZE_TYPE__ size, const std::nothrow_t&) noexcept;
void* operator new[](__SIZE_TYPE__ size, const std::nothrow_t&) noexcept;
void* operator new(__SIZE_TYPE__ size);
void* operator new[](__SIZE_TYPE__ size);

// Global operator delete - uses kfree
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

// Sized delete variants (C++14)
void operator delete(void* ptr, __SIZE_TYPE__) noexcept;
void operator delete[](void* ptr, __SIZE_TYPE__) noexcept;
