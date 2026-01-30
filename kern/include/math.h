#pragma once

#define do_div(n, base) ({                               \
    unsigned long __upper, __low, __high, __mod, __base; \
    __base = (base);                                     \
    asm(""                                               \
        : "=a"(__low), "=d"(__high)                      \
        : "A"(n));                                       \
    __upper = __high;                                    \
    if (__high != 0) {                                   \
        __upper = __high % __base;                       \
        __high = __high / __base;                        \
    }                                                    \
    asm("divl %2"                                        \
        : "=a"(__low), "=d"(__mod)                       \
        : "rm"(__base), "0"(__low), "1"(__upper));       \
    asm(""                                               \
        : "=A"(n)                                        \
        : "a"(__low), "d"(__high));                      \
    __mod;                                               \
})

// Type trait for pointer detection
template<typename T> struct is_pointer { static constexpr bool value = false; };
template<typename T> struct is_pointer<T*> { static constexpr bool value = true; };

// Helper to convert any type to uintptr_t
template <typename T>
inline uintptr_t to_uint(T a) {
    if constexpr (is_pointer<T>::value) {
        return reinterpret_cast<uintptr_t>(a);
    } else {
        return static_cast<uintptr_t>(a);
    }
}

// Helper to convert uintptr_t back to original type
template <typename T>
inline T from_uint(uintptr_t val) {
    if constexpr (is_pointer<T>::value) {
        return reinterpret_cast<T>(val);
    } else {
        return static_cast<T>(val);
    }
}

template <typename T, typename U>
inline T round_down(T a, U n) {
    auto ua = to_uint(a);
    return from_uint<T>(ua - ua % n);
}

template <typename T, typename U>
inline T round_up(T a, U n) {
    auto ua = to_uint(a);
    return from_uint<T>((ua + n - 1) / n * n);
}

template <typename T>
inline T min(T a, T b) {
    return (a < b) ? a : b;
}
template <typename T>
inline T max(T a, T b) {
    return (a > b) ? a : b;
}
