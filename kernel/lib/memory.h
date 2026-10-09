#pragma once

#include <base/types.h>
#include <asm/arch.h>
#include <sys/new.hpp>

// memset/memcpy are provided as extern "C" symbols in kernel/cxxrt.cpp
// so that clang can resolve implicit calls (struct zeroing, assignment, etc.)
extern "C" void* memset(void* s, int c, size_t n);
extern "C" void* memcpy(void* dst, const void* src, size_t n);
extern "C" void* memmove(void* dst, const void* src, size_t n);
extern "C" int memcmp(const void* s1, const void* s2, size_t n);

// Forward declarations for kernel memory allocation
void* kmalloc(size_t size);
void kfree(void* ptr);

// sys/new.hpp owns the shared allocation declarations and placement definitions.
// The kernel provides the allocating/freeing definitions in cxxrt.cpp.
