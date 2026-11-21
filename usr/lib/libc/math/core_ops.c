/*
 * round, trunc, nearbyint and rint with their long/long long
 * partners, plus fmin, fmax, fdim, nextafter, nan and fma.
 *
 * The rounding helpers strip the fraction with integer bit masks,
 * exact in every rounding mode.  round() breaks ties away from
 * zero, rint()/nearbyint() use to-nearest-even; BRK exposes no
 * floating-point environment (there is no fenv.h), so nearbyint and
 * rint are the same function here.  The long/long long variants
 * saturate at the type boundary, which is what glibc does.
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libm.h"

/* Fraction bits below the ones place for unbiased exponent e. */
#define CORE_FRAC_MASK(e) (((1ULL << (52 - (e))) - 1))
#define CORE_HALF(e) (1ULL << (51 - (e)))

/* Magnitude >= 0.5, exact for any double including subnormals. */
static int mag_ge_half(double x)
{
	return (asu64(x) & ~(1ULL << 63)) >= 0x3fe0000000000000ULL;
}

double trunc(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;

	if (u.i << 1 == 0 || e >= 52)
		return x;			/* +-0, integral, inf, nan */
	if (e < 0)
		return fro64(u.i & (1ULL << 63));/* signed zero */
	u.i -= u.i & CORE_FRAC_MASK(e);
	return u.f;
}

double round(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;
	uint64_t frac;

	if (u.i << 1 == 0 || e >= 52)
		return x;
	if (e < 0) {
		/* |x| < 1: ties and above go away from zero. */
		if (mag_ge_half(x))
			return u.i >> 63 ? -1.0 : 1.0;
		return fro64(u.i & (1ULL << 63));
	}
	frac = u.i & CORE_FRAC_MASK(e);
	if (!frac)
		return x;
	u.i -= frac;
	if (frac >= CORE_HALF(e)) {
		/* away from zero: bit patterns grow with the magnitude
		   for either sign, so the step is always an increment */
		u.i += 1ULL << (52 - e);
	}
	return u.f;
}

double rint(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;
	uint64_t frac;

	if (u.i << 1 == 0 || e >= 52)
		return x;
	if (e < 0) {
		/* |x| < 1: only |x| > 0.5 leaves zero; 0.5 ties to 0. */
		if (mag_ge_half(x) &&
		    (asu64(x) & ~(1ULL << 63)) != 0x3fe0000000000000ULL)
			return u.i >> 63 ? -1.0 : 1.0;
		return fro64(u.i & (1ULL << 63));
	}
	frac = u.i & CORE_FRAC_MASK(e);
	if (!frac)
		return x;
	u.i -= frac;
	/* more than half, or exactly half onto an odd integer:
	   step to the even neighbor */
	if (frac > CORE_HALF(e) ||
	    (frac == CORE_HALF(e) && ((u.i >> (52 - e)) & 1)))
		u.i += 1ULL << (52 - e);
	return u.f;
}

double nearbyint(double x)
{
	return rint(x);
}

/* Saturating conversion of an already-integral double. */
static long to_long_sat(double r)
{
	if (isnan(r))
		return 0;
	if (r >= 9223372036854775808.0)		/* 2^63 */
		return LONG_MAX;
	if (r < -9223372036854775808.0)
		return LONG_MIN;
	return (long)r;
}

long lround(double x)
{
	return to_long_sat(round(x));
}

long long llround(double x)
{
	return (long long)to_long_sat(round(x));
}

long lrint(double x)
{
	return to_long_sat(rint(x));
}

long long llrint(double x)
{
	return (long long)to_long_sat(rint(x));
}

double fmin(double x, double y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;
	if (signbit(x) != signbit(y))
		return signbit(x) ? x : y;	/* the negative one, so
						   fmin(+0,-0) is -0 */
	return x < y ? x : y;
}

double fmax(double x, double y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;
	if (signbit(x) != signbit(y))
		return signbit(x) ? y : x;	/* the positive one, so
						   fmax(+0,-0) is +0 */
	return x > y ? x : y;
}

double fdim(double x, double y)
{
	if (isnan(x) || isnan(y))
		return x + y;
	return x > y ? x - y : 0.0;
}

double nextafter(double x, double y)
{
	union brk_dbits ux = { x }, uy = { y };

	if (isnan(x) || isnan(y))
		return x + y;
	if (ux.i == uy.i)
		return y;			/* includes +0 == +0 */
	if ((ux.i << 1) == 0) {
		/* step off signed zero toward y */
		ux.i = 1;
		return copysign(ux.f, y);
	}
	/*
	 * Bit patterns grow with the magnitude for either sign, so
	 * moving away from zero is ux.i++ for both.  Toward zero is
	 * the one direction that can cross into the subnormal range,
	 * which the bit decrement handles exactly.
	 */
	if ((x > 0) ? (y < x) : (y > x))
		ux.i--;
	else
		ux.i++;
	return ux.f;
}

double nan(const char *tagp)
{
	char buf[64];

	if (!tagp || !*tagp)
		return NAN;
	snprintf(buf, sizeof(buf), "nan(%s)", tagp);
	return strtod(buf, NULL);
}

/*
 * fma is the fused multiply-add.  The RISC-V target lowers
 * __builtin_fma to the fmadd.d instruction, and the host compilers
 * do the same or call their own fma, so no libgcc helper appears.
 */
double fma(double x, double y, double z)
{
	return __builtin_fma(x, y, z);
}
