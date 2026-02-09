#pragma once

typedef __INT8_TYPE__ int8_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;

typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef unsigned long long size_t;

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
