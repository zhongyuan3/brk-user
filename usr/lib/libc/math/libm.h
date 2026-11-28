/*
 * BRK libc-internal math header.
 *
 * The math directory is a from-scratch implementation: no code here is
 * derived from another libc.  The irrational constants and the fitted
 * polynomial coefficients used by the transcendentals are produced by
 * scripts/gen-libm-consts.py, which computes them from the defining
 * series at extended precision; the generated values are committed
 * alongside the code.
 *
 * Design notes shared by the transcendental kernels:
 *   - IEEE-754 binary64 only, round-to-nearest assumed (the exact
 *     functions -- floor/ceil/modf/frexp/ldexp/scalbn/fmod -- work in
 *     every rounding mode; the series kernels do not).
 *   - Argument reduction is kept exact in doubles as far as the format
 *     allows; the remainder beyond that is carried in hi/lo
 *     compensated pairs built with two-sum.
 *   - Polynomial corrections are evaluated in factored form
 *     (x + x^3*P(x^2)) so the rounding error of the correction is
 *     suppressed by the factored powers and stays far below the final
 *     rounding.
 */

#ifndef _LIBM_H
#define _LIBM_H

#include <math.h>
#include <stdint.h>

#ifndef hidden
#define hidden __attribute__((visibility("hidden")))
#endif

/* Bit-level reinterpretation of a double.  The macro parameter names
 * deliberately avoid the member names. */
#define asu64(v) ((union brk_dbits){ (v) }.i)
#define fro64(b) ((union brk_dbits){ .i = (b) }.f)

union brk_dbits {
	double f;
	uint64_t i;
};

/* Round z to the nearest integer.  The magic constant 1.5*2^52 puts the
 * sum in [2^52, 2^53) where every double is an integer, so the result
 * is exact (round-to-nearest) for |z| < 2^51. */
static inline double rnd_to_int(double z)
{
	return z + 6755399441055744.0 - 6755399441055744.0;
}

/* Exact rounding error of s = a + b (Dekker's two-sum; the operands may
 * come in either order). */
static inline double two_sum_err(double a, double b, double s)
{
	double bb = s - a;

	return (a - (s - bb)) + (b - bb);
}

/* Exact rounding error of s = a - b. */
static inline double two_diff_err(double a, double b, double s)
{
	double bb = s - a;

	return (a - (s - bb)) - (b + bb);
}

/* Rounding error of p = fl(a*b), Dekker's two-product: both operands
 * are split, every partial product except the tiny alo*blo term is
 * exact, so the returned error is good to about 2^-100 of the
 * product.  Requires |a|, |b| < 2^975 so the splits cannot overflow. */
static inline double two_prod_err(double a, double b, double p)
{
	double ca = 134217729.0 * a;
	double cb = 134217729.0 * b;
	double ahi = ca - (ca - a);
	double alo = a - ahi;
	double bhi = cb - (cb - b);
	double blo = b - bhi;

	return ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
}

hidden int __brk_rem_pio2(double ax, double *rhi, double *rlo);
hidden double __brk_exp_pair(double hi, double lo);
hidden void __brk_exp_full(double x, double lo, double *hi, double *elo);
hidden double __brk_scale_pow2(double v, int m);
hidden void __brk_log_pair(double x, double *hi, double *lo);
hidden void __brk_ln1p_pair(double f, double *hi, double *lo);
hidden double __brk_asin_poly(double t);
hidden double __brk_atan_core(double u);

#endif /* _LIBM_H */
