#pragma once

// The instructions the shared libm series in math_series.cpp needs, which
// each architecture supplies in user/gui/math_<arch>.cpp.
namespace zonix_math {

// Rounds towards zero and to the nearest integer, ties to even.
double truncate(double x);
double nearest(double x);
double root(double x);

}  // namespace zonix_math
