#ifndef _MATH_H
#define _MATH_H

/*
 * BRK libc math header.
 *
 * The double-precision implementations under usr/lib/libc/math are a
 * from-scratch BRK implementation; the irrational constants and fitted
 * polynomial coefficients it uses are produced by
 * scripts/gen-libm-consts.py from the defining series at extended
 * precision.  This header covers the public surface needed by C99
 * programs and the math code itself.  Only double precision is
 * provided; float variants fall back to double via promotion so that
 * any float-prototype user links successfully.
 */

#define M_E 2.7182818284590452354 /* e */
#define M_LOG2E 1.4426950408889634074 /* log_2 e */
#define M_LOG10E 0.43429448190325182765 /* log_10 e */
#define M_LN2 0.69314718055994530942 /* log_e 2 */
#define M_LN10 2.30258509299404568402 /* log_e 10 */
#define M_PI 3.14159265358979323846 /* pi */
#define M_PI_2 1.57079632679489661923 /* pi/2 */
#define M_PI_4 0.78539816339744830962 /* pi/4 */
#define M_1_PI 0.31830988618379067154 /* 1/pi */
#define M_2_PI 0.63661977236758134308 /* 2/pi */
#define M_2_SQRTPI 1.12837916709551257390 /* 2/sqrt(pi) */
#define M_SQRT2 1.41421356237309504880 /* sqrt(2) */
#define M_SQRT1_2 0.70710678118654752440 /* 1/sqrt(2) */

#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nan("")

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

typedef float float_t;
typedef double double_t;

double fabs(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double floor(double x);
double ceil(double x);
double sqrt(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double scalbn(double x, int exp);
double copysign(double x, double y);

/* C99 rounding and comparison helpers (core_ops.c). */
double trunc(double x);
double round(double x);
double rint(double x);
double nearbyint(double x);
long lround(double x);
long long llround(double x);
long lrint(double x);
long long llrint(double x);
double fmin(double x, double y);
double fmax(double x, double y);
double fdim(double x, double y);
double nextafter(double x, double y);
double nan(const char *tagp);
double fma(double x, double y, double z);
double remainder(double x, double y);
double remquo(double x, double y, int *quo);
double log1p(double x);
double expm1(double x);
double exp2(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double hypot(double x, double y);
double cbrt(double x);

int __signbit(double x);
int __fpclassify(double x);

#define signbit(x) __signbit(x)
#define isnan(x) ((x) != (x))
#define isinf(x) (__fpclassify(x) == FP_INFINITE)
#define isfinite(x) (__fpclassify(x) > FP_INFINITE)
#define isnormal(x) (__fpclassify(x) == FP_NORMAL)
#define fpclassify(x) __fpclassify(x)

#endif
