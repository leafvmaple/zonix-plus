#pragma once

#include <base/types.h>
#include <sys/type_traits.hpp>

// Helper to convert any type to uintptr_t
template<typename T>
inline uintptr_t to_uint(T a) {
    if constexpr (sys::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(a);
    } else {
        return static_cast<uintptr_t>(a);
    }
}

// Helper to convert uintptr_t back to original type
template<typename T>
inline T from_uint(uintptr_t val) {
    if constexpr (sys::is_pointer_v<T>) {
        return reinterpret_cast<T>(val);
    } else {
        return static_cast<T>(val);
    }
}

template<typename T, typename U>
inline T round_down(T a, U n) {
    auto ua = to_uint(a);
    return from_uint<T>(ua - ua % n);
}

template<typename T, typename U>
inline T round_up(T a, U n) {
    auto ua = to_uint(a);
    return from_uint<T>((ua + n - 1) / n * n);
}
