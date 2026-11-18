#include <sys/cmath.hpp>

extern "C" float fabsf(float x) {
    return __builtin_bit_cast(float, __builtin_bit_cast(unsigned int, x) & 0x7FFFFFFFU);
}

extern "C" float floorf(float x) {
    // Above 2^23 every finite float is integral. Preserve signed zero and NaN.
    if (!(x > -8388608.f && x < 8388608.f) || x == 0.f)
        return x;
    int value = static_cast<int>(x);
    return static_cast<float>(value - (static_cast<float>(value) > x ? 1 : 0));
}

extern "C" float ceilf(float x) {
    return -floorf(-x);
}

extern "C" float sqrtf(float x) {
    float result;
    asm("sqrtss %1, %0" : "=x"(result) : "x"(x));
    return result;
}

extern "C" float sinf(float x) {
    float value;
    asm("flds %1; fsin; fstps %0" : "=m"(value) : "m"(x) : "st");
    return value;
}

extern "C" double cos(double x) {
    double value;
    asm("fldl %1; fcos; fstpl %0" : "=m"(value) : "m"(x) : "st");
    return value;
}

extern "C" float cosf(float x) {
    return static_cast<float>(cos(x));
}

extern "C" double ldexp(double x, int power) {
    double value;
    asm("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0" : "=m"(value) : "m"(x), "m"(power) : "st", "st(1)");
    return value;
}

extern "C" float fmodf(float x, float y) {
    float value;
    asm("flds %2; flds %1; 1: fprem; fnstsw %%ax; testw $0x400, %%ax; jnz 1b; fstp %%st(1); fstps %0"
        : "=m"(value)
        : "m"(x), "m"(y)
        : "ax", "cc", "st", "st(1)");
    return value;
}

extern "C" float acosf(float x) {
    float height = sqrtf((1.f - x) * (1.f + x));
    float result;
    asm("flds %1; flds %2; fpatan; fstps %0" : "=m"(result) : "m"(height), "m"(x) : "st", "st(1)");
    return result;
}

extern "C" double pow(double base, double exponent) {
    // The font/image paths use nonnegative bases. Use x87 to evaluate
    // 2^(exponent * log2(base)) without adding a hosted libm dependency.
    if (exponent == 0.0 || base == 1.0)
        return 1.0;
    if (base == 0.0)
        return exponent > 0.0 ? 0.0 : __builtin_inf();
    if (!(base > 0.0))
        return __builtin_nan("");
    double value;
    asm("fldl %2; fldl %1; fyl2x; fld %%st(0); frndint; fxch; fsub %%st(1), %%st; "
        "f2xm1; fld1; faddp; fscale; fstp %%st(1); fstpl %0"
        : "=m"(value)
        : "m"(base), "m"(exponent)
        : "st", "st(1)", "st(2)");
    return value;
}

extern "C" float powf(float base, float exponent) {
    return static_cast<float>(pow(base, exponent));
}
