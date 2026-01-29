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

#define ROUND_DOWN(a, n) ({        \
    uint32_t __a = (uint32_t)(a);     \
    (typeof(a))(__a - __a % (n)); \
})

/* Round up to the nearest multiple of n */
#define ROUND_UP(a, n) ({                                   \
    uint32_t __n = (uint32_t)(n);                           \
    (typeof(a))(ROUND_DOWN((uint32_t)(a) + __n - 1, __n));  \
})

template <typename T>
inline T min(T a, T b) {
    return (a < b) ? a : b;
}
template <typename T>
inline T max(T a, T b) {
    return (a > b) ? a : b;
}
