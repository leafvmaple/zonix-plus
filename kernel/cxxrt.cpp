// cxxrt.cpp — Minimal C/C++ runtime stubs required by clang in freestanding mode
//
// Clang may emit calls to memset/memcpy even with -fno-builtin (for struct
// zeroing, assignment, etc.) and expects linkable symbols.  It also needs
// __cxa_pure_virtual (pure virtual call fallback) and atexit (global
// destructor registration, which we ignore in a kernel).
//
// operator new/delete must not be declared inline (Clang -Winline-new-delete),
// so they live here as non-inline definitions.

#include <lib/memory.h>
#include <asm/arch.h>

extern "C" {

void* memset(void* s, int c, size_t n) {
    return arch_memset(s, c, n);
}

void* memcpy(void* dst, const void* src, size_t n) {
    return arch_memcpy(dst, src, n);
}

void* memmove(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    if (d < s) {
        while (n-- > 0)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n-- > 0)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const auto* p1 = static_cast<const unsigned char*>(s1);
    const auto* p2 = static_cast<const unsigned char*>(s2);
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void __cxa_pure_virtual() {
    // Pure virtual function was called — should never happen.
    // Hang to make the fault visible during debugging.
    arch_halt_forever();
}

int atexit(void (*)()) {
    // No-op: kernel never exits, so we never run global destructors.
    return 0;
}

}  // extern "C"

void* operator new(__SIZE_TYPE__ size, const std::nothrow_t&) noexcept {
    return kmalloc(size);
}

void* operator new[](__SIZE_TYPE__ size, const std::nothrow_t&) noexcept {
    return kmalloc(size);
}

void* operator new(__SIZE_TYPE__ size) {
    return operator new(size, std::nothrow);
}

void* operator new[](__SIZE_TYPE__ size) {
    return operator new[](size, std::nothrow);
}

void operator delete(void* ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}

void operator delete(void* ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}
