#pragma once

void panic_at(const char* file, int line, const char* fmt, ...);

// zstl supplies assert and its NDEBUG policy; the kernel supplies diagnostics.
#undef ZSTL_ASSERT_FAILURE
#define ZSTL_ASSERT_FAILURE(expression, file, line) ::panic_at(file, line, "assertion failed: %s", expression)

#define PANIC(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#include <sys/cassert.hpp>
