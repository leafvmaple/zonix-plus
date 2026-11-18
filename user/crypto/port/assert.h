#pragma once

#include "minilib.h"

#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
#define assert(condition) ((condition) ? (void)0 : sys_exit(1))
#endif
