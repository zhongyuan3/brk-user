/*
 * scalbn and ldexp: multiply by 2^n.
 *
 * The scaling is done in steps of at most one exponent field so every
 * intermediate is exact; the final multiply rounds only once, even when
 * the result lands in the subnormal range.
 */

#include <stdint.h>

#include "libm.h"

double scalbn(double x, int n)
{
	union brk_dbits u;

	if (n > 1023) {
		x *= 0x1p1023;
		n -= 1023;
		if (n > 1023) {
			x *= 0x1p1023;
			n -= 1023;
			if (n > 1023)
				n = 1023;    /* overflow is already inevitable */
		}
	} else if (n < -1022) {
		/* 2^-969 = 2^-1022 * 2^53: the pre-scale keeps the last
		   multiply out of the double-rounding subnormal window. */
		x *= 0x1p-969;
		n += 969;
		if (n < -1022) {
			x *= 0x1p-969;
			n += 969;
			if (n < -1022)
				n = -1022;
		}
	}
	u.i = (uint64_t)(0x3ff + n) << 52;
	return x * u.f;
}

double ldexp(double x, int n)
{
	return scalbn(x, n);
}
