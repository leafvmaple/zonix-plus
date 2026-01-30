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

#ifdef __cplusplus
template<typename T, typename M>
constexpr size_t offset_of(M T::*member) {
    return reinterpret_cast<size_t>(&(static_cast<T*>(nullptr)->*member));
}

template<typename T, typename M>
inline T* to_struct(void* ptr, M T::*member) {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) - offset_of(member));
}

#define OFFSET_OF(type, member) \
    reinterpret_cast<size_t>(&(static_cast<type*>(nullptr)->member))
#define TO_STRUCT(ptr, type, member) \
    reinterpret_cast<type*>(reinterpret_cast<char*>(ptr) - OFFSET_OF(type, member))
#else
#define offset_of(type, member) __builtin_offsetof(type, member)
#define to_struct(ptr, type, member) \
    ((type *)((char *)(ptr) - offset_of(type, member)))
#define OFFSET_OF(type, member) offset_of(type, member)
#define TO_STRUCT(ptr, type, member) to_struct(ptr, type, member)
#endif

#ifndef NULL
#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL ((void *)0)
#endif
#endif
