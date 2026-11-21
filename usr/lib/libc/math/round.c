/*
 * floor, ceil and modf.
 *
 * The fraction is stripped with integer bit masks, which is exact in
 * every rounding mode and never goes through floating point arithmetic.
 */

#include <stdint.h>

#include "libm.h"

/* Number of fraction bits below the ones place for exponent e
 * (unbiased, 0 <= e < 52). */
#define FRAC_MASK(e) (((1ULL << (52 - (e))) - 1))

double floor(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;

	if (u.i << 1 == 0 || e >= 52)
		return x;                    /* +-0, integral, inf or nan */
	if (e < 0) {
		/* |x| < 1: the neighbors are 0 and -1. */
		if (u.i >> 63)
			return -1.0;
		return 0.0;
	}
	if ((u.i & FRAC_MASK(e)) == 0)
		return x;
	u.i -= u.i & FRAC_MASK(e);      /* truncate toward zero */
	if (u.i >> 63)
		u.i += 1ULL << (52 - e); /* negative: step away from zero */
	return u.f;
}

double ceil(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;

	if (u.i << 1 == 0 || e >= 52)
		return x;
	if (e < 0) {
		/* |x| < 1: the neighbors are -0 and 1. */
		if (u.i >> 63)
			return -0.0;
		return 1.0;
	}
	if ((u.i & FRAC_MASK(e)) == 0)
		return x;
	u.i -= u.i & FRAC_MASK(e);
	if (!(u.i >> 63))
		u.i += 1ULL << (52 - e); /* positive: step away from zero */
	return u.f;
}

double modf(double x, double *iptr)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;

	if (e >= 52 || u.i << 1 == 0) {
		/* integral, inf, nan or zero: the fraction is a signed 0
		   (the nan keeps its payload and also reports through the
		   fraction, matching the usual conventions). */
		*iptr = x;
		u.i &= 1ULL << 63;
		return u.f;
	}
	if (e < 0) {
		/* |x| < 1: the integral part is a signed zero */
		u.i &= 1ULL << 63;
		*iptr = u.f;
		return x;
	}
	if ((u.i & FRAC_MASK(e)) == 0) {
		*iptr = x;
		u.i &= 1ULL << 63;
		return u.f;
	}
	u.i -= u.i & FRAC_MASK(e);       /* integral part, exact */
	*iptr = u.f;
	return x - u.f;                  /* exact: shares x's high bits */
}
