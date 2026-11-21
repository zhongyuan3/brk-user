/*
 * asinh(x) = ln(x + sqrt(x^2 + 1)).
 *
 * For |x| < 0.5 the argument is reworked for the ln(1+f) core:
 *
 *     asinh(x) = ln1p(|x| + |x|^2 / (1 + sqrt(1+x^2)))
 *
 * where the rationalized sqrt keeps everything cancellation-free
 * as x -> 0 (asinh(x) ~ x there).  From 0.5 upward the direct
 * argument x + sqrt(x^2+1) no longer cancels and the log pair runs
 * on it; past 2^27 the sqrt is 1 to 2^-54 relative, so asinh(x) is
 * log(2|x|) -- assembled as log(|x|) + ln 2, because forming 2|x|
 * could overflow.
 */

#include <math.h>

#include "libm.h"

#define LN2_HI 0x1.62e42ff000000p-1	/* 33 significant bits */
#define LN2_LO -0x1.718432a1b0e26p-35

double asinh(double x)
{
	double a = fabs(x);
	double hi, lo;

	if (isnan(x))
		return x;
	if (a < 2.0 / 3.0 * 0x1p-52)
		return x;			/* asinh(x) == x exactly */
	if (a < 0.5) {
		double u = a + a * a / (1.0 + sqrt(1.0 + a * a));

		__brk_ln1p_pair(u, &hi, &lo);
		return copysign(hi + lo, x);
	}
	if (a >= 0x1p27) {
		double s = hi, err;

		__brk_log_pair(a, &hi, &lo);
		s = hi + LN2_HI;
		err = two_sum_err(hi, LN2_HI, s);
		return copysign(s + (err + lo + LN2_LO), x);
	}
	__brk_log_pair(a + sqrt(1.0 + a * a), &hi, &lo);
	return copysign(hi + lo, x);
}
