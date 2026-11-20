#include "math_primitives.h"

// Rounding and square roots are single AArch64 instructions.

namespace zonix_math {

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

}  // namespace zonix_math

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
