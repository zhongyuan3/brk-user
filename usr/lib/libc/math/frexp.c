/*
 * frexp: split a double into a fraction in [+1/2, 1) and a power of two.
 *
 * Exact bit manipulation in every rounding mode.
 */

#include <stdint.h>

#include "libm.h"

double frexp(double x, int *e)
{
	union brk_dbits u = { x };
	int ee = (int)(u.i >> 52) & 0x7ff;

	if (ee == 0x7ff) {
		/* inf or nan: nothing to split */
		*e = 0;
		return x;
	}
	if (ee == 0) {
		/* zero or subnormal */
		if (x == 0.0) {
			*e = 0;
			return x;        /* preserves the sign of zero */
		}
		/* Every subnormal becomes normal after one exact multiply
		   by 2^64 (the subnormal range spans only 52 bits). */
		x *= 0x1p64;
		u.f = x;
		ee = (int)(u.i >> 52) & 0x7ff;
		*e = ee - 0x3fe - 64;
	} else {
		*e = ee - 0x3fe;
	}
	u.i &= 0x800fffffffffffffull;
	u.i |= 0x3fe0000000000000ull;
	return u.f;
}
