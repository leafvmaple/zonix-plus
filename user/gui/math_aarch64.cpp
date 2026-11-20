#include <sys/cmath.hpp>

// The libm functions mini-cocos uses, without a hosted libm. Rounding and
// square roots are single instructions; the transcendental functions reduce
// their argument and evaluate a short series in double precision, which is
// more than the font, image and transform paths need.

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double HALF_PI = PI / 2.0;
constexpr double LN2 = 0.69314718055994530942;
constexpr double SQRT3 = 1.73205080756887729353;

double truncate(double x) {
    double result;
    asm("frintz %d0, %d1" : "=w"(result) : "w"(x));
    return result;
}

double nearest(double x) {
    double result;
    asm("frintn %d0, %d1" : "=w"(result) : "w"(x));
    return result;
}

double root(double x) {
    double result;
    asm("fsqrt %d0, %d1" : "=w"(result) : "w"(x));
    return result;
}

// sin and cos of r in [-pi/4, pi/4].
double sine_series(double r) {
    double square = r * r;
    double term = r;
    double sum = r;
    for (int n = 1; n <= 7; ++n) {
        term *= -square / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

double cosine_series(double r) {
    double square = r * r;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 7; ++n) {
        term *= -square / ((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

// sin(x) for `phase` 0 and cos(x) for `phase` 1: x is reduced to a quarter
// turn r and a quadrant, and cos(x) = sin(x + pi/2).
double sine(double x, int phase) {
    if (x != x || x - x != 0.0)
        return __builtin_nan("");
    double turns = nearest(x / HALF_PI);
    double r = x - turns * HALF_PI;
    long quadrant = (static_cast<long>(turns) + phase) & 3;
    switch (quadrant) {
        case 0: return sine_series(r);
        case 1: return cosine_series(r);
        case 2: return -sine_series(r);
        default: return -cosine_series(r);
    }
}

// atan(t) for t >= 0.
double arctangent(double t) {
    bool inverted = t > 1.0;
    if (inverted)
        t = 1.0 / t;
    // Above tan(pi/12), atan(t) = pi/6 + atan((t*sqrt3 - 1) / (sqrt3 + t)).
    bool shifted = t > 2.0 - SQRT3;
    if (shifted)
        t = (t * SQRT3 - 1.0) / (SQRT3 + t);
    double square = t * t;
    double power = t;
    double sum = 0.0;
    for (int n = 0; n < 8; ++n) {
        sum += (n % 2 == 0 ? power : -power) / (2 * n + 1);
        power *= square;
    }
    if (shifted)
        sum += PI / 6.0;
    return inverted ? HALF_PI - sum : sum;
}

// ln(x) for finite x > 0, from x = m * 2^e with m in [sqrt(1/2), sqrt(2)).
double logarithm(double x) {
    auto bits = __builtin_bit_cast(unsigned long, x);
    long exponent = static_cast<long>((bits >> 52) & 0x7FF);
    if (exponent == 0) {
        // Subnormal: scale into the normal range first.
        x *= 0x1p54;
        bits = __builtin_bit_cast(unsigned long, x);
        exponent = static_cast<long>((bits >> 52) & 0x7FF) - 54;
    }
    exponent -= 1023;
    double mantissa = __builtin_bit_cast(double, (bits & 0x000FFFFFFFFFFFFFUL) | 0x3FF0000000000000UL);
    if (mantissa > 1.41421356237309504880) {
        mantissa /= 2.0;
        ++exponent;
    }
    // ln(m) = 2 atanh(s) with s = (m - 1) / (m + 1), |s| < 0.172.
    double s = (mantissa - 1.0) / (mantissa + 1.0);
    double square = s * s;
    double power = s;
    double sum = 0.0;
    for (int n = 0; n < 10; ++n) {
        sum += power / (2 * n + 1);
        power *= square;
    }
    return 2.0 * sum + static_cast<double>(exponent) * LN2;
}

double exponential(double y) {
    if (y > 709.0)
        return __builtin_inf();
    if (y < -745.0)
        return 0.0;
    double k = nearest(y / LN2);
    double r = y - k * LN2;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 13; ++n) {
        term *= r / n;
        sum += term;
    }
    return ldexp(sum, static_cast<int>(k));
}

}  // namespace

extern "C" float fabsf(float x) {
    return __builtin_bit_cast(float, __builtin_bit_cast(unsigned int, x) & 0x7FFFFFFFU);
}

extern "C" float floorf(float x) {
    float result;
    asm("frintm %s0, %s1" : "=w"(result) : "w"(x));
    return result;
}

extern "C" float ceilf(float x) {
    float result;
    asm("frintp %s0, %s1" : "=w"(result) : "w"(x));
    return result;
}

extern "C" float sqrtf(float x) {
    float result;
    asm("fsqrt %s0, %s1" : "=w"(result) : "w"(x));
    return result;
}

extern "C" float sinf(float x) {
    return static_cast<float>(sine(x, 0));
}

extern "C" double cos(double x) {
    return sine(x, 1);
}

extern "C" float cosf(float x) {
    return static_cast<float>(cos(x));
}

extern "C" double ldexp(double x, int power) {
    while (power > 1023) {
        x *= 0x1p1023;
        power -= 1023;
    }
    while (power < -1022) {
        x *= 0x1p-1022;
        power += 1022;
    }
    return x * __builtin_bit_cast(double, static_cast<unsigned long>(power + 1023) << 52);
}

extern "C" float fmodf(float x, float y) {
    if (y == 0.f || x != x || y != y || x - x != 0.f)
        return __builtin_nanf("");
    double divisor = y;
    return static_cast<float>(x - truncate(x / divisor) * divisor);
}

extern "C" float acosf(float x) {
    if (!(x >= -1.f && x <= 1.f))
        return __builtin_nanf("");
    if (x == -1.f)
        return static_cast<float>(PI);
    // acos(x) = 2 atan(sqrt((1 - x) / (1 + x))).
    double value = x;
    return static_cast<float>(2.0 * arctangent(root((1.0 - value) / (1.0 + value))));
}

extern "C" double pow(double base, double exponent) {
    // The font/image paths use nonnegative bases.
    if (exponent == 0.0 || base == 1.0)
        return 1.0;
    if (base == 0.0)
        return exponent > 0.0 ? 0.0 : __builtin_inf();
    if (!(base > 0.0))
        return __builtin_nan("");
    if (base - base != 0.0)
        return exponent > 0.0 ? base : 0.0;
    return exponential(exponent * logarithm(base));
}

extern "C" float powf(float base, float exponent) {
    return static_cast<float>(pow(base, exponent));
}
