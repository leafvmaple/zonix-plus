#pragma once

#include <base/types.h>
#include "lib/stdarg.h"
#include <sys/cstdio.hpp>

int cprintf(const char* fmt, ...);
int vcprintf(const char* fmt, va_list args);
