#pragma once

typedef char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef int32_t intptr_t;
typedef uint32_t uintptr_t;

typedef uint32_t size_t;

#define offset_of(type, member) \
    ((size_t)(&((type *)0)->member))

#define to_struct(ptr, type, member) \
    ((type *)((char *)(ptr) - offset_of(type, member)))

#ifndef NULL
#define NULL ((void *)0)
#endif
