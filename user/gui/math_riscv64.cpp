#include "math_primitives.h"

// RV64GC has square roots but no rounding instruction without Zfa. Rounding
// converts to an integer with an explicit rounding mode and back. Values too
// large to convert are already integral, and NaN and infinity are returned as
// they are. The sign is copied back so that, for example, -0.5 truncates to -0.

namespace {

constexpr unsigned long DOUBLE_SIGN = 1UL << 63;
constexpr unsigned int FLOAT_SIGN = 1U << 31;
// From 2^52 (double) and 2^23 (float) on, every finite value is integral.
constexpr double DOUBLE_INTEGRAL = 0x1p52;
constexpr float FLOAT_INTEGRAL = 0x1p23f;

double with_sign_of(double value, double sign) {
    auto bits = __builtin_bit_cast(unsigned long, value) & ~DOUBLE_SIGN;
    return __builtin_bit_cast(double, bits | (__builtin_bit_cast(unsigned long, sign) & DOUBLE_SIGN));
}

float with_sign_of(float value, float sign) {
    auto bits = __builtin_bit_cast(unsigned int, value) & ~FLOAT_SIGN;
    return __builtin_bit_cast(float, bits | (__builtin_bit_cast(unsigned int, sign) & FLOAT_SIGN));
}

bool integral(double x) {
    return !(x > -DOUBLE_INTEGRAL && x < DOUBLE_INTEGRAL);
}

bool integral(float x) {
    return !(x > -FLOAT_INTEGRAL && x < FLOAT_INTEGRAL);
}

// The rounding mode is part of the instruction, so each mode has its own
// conversion.
#define ZONIX_ROUND_DOUBLE(name, mode)                                  \
    double name(double x) {                                             \
        if (integral(x))                                                \
            return x;                                                   \
        long whole;                                                     \
        double result;                                                  \
        asm("fcvt.l.d %0, %1, " mode : "=r"(whole) : "f"(x));           \
        asm("fcvt.d.l %0, %1" : "=f"(result) : "r"(whole));             \
        return with_sign_of(result, x);                                 \
    }

#define ZONIX_ROUND_FLOAT(name, mode)                                   \
    float name(float x) {                                               \
        if (integral(x))                                                \
            return x;                                                   \
        int whole;                                                      \
        float result;                                                   \
        asm("fcvt.w.s %0, %1, " mode : "=r"(whole) : "f"(x));           \
        asm("fcvt.s.w %0, %1" : "=f"(result) : "r"(whole));             \
        return with_sign_of(result, x);                                 \
    }

ZONIX_ROUND_DOUBLE(round_towards_zero, "rtz")
ZONIX_ROUND_DOUBLE(round_to_nearest, "rne")
ZONIX_ROUND_FLOAT(round_down, "rdn")
ZONIX_ROUND_FLOAT(round_up, "rup")

#undef ZONIX_ROUND_DOUBLE
#undef ZONIX_ROUND_FLOAT

}  // namespace

namespace zonix_math {

double truncate(double x) {
    return round_towards_zero(x);
}

double nearest(double x) {
    return round_to_nearest(x);
}

double root(double x) {
    double result;
    asm("fsqrt.d %0, %1" : "=f"(result) : "f"(x));
    return result;
}

}  // namespace zonix_math

extern "C" float floorf(float x) {
    return round_down(x);
}

extern "C" float ceilf(float x) {
    return round_up(x);
}

extern "C" float sqrtf(float x) {
    float result;
    asm("fsqrt.s %0, %1" : "=f"(result) : "f"(x));
    return result;
}
