#pragma once

typedef __builtin_va_list va_list;

// NOLINTNEXTLINE(readability-identifier-naming)
#define va_start(ap, last) (__builtin_va_start(ap, last))
// NOLINTNEXTLINE(readability-identifier-naming)
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
// NOLINTNEXTLINE(readability-identifier-naming)
#define va_end(ap) /*nothing*/