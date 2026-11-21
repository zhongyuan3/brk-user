/*
 * acosh(x) = ln(x + sqrt(x^2 - 1)), x >= 1.
 *
 * With t = x - 1 the argument is reworked for log1p:
 *
 *     acosh(x) = log1p(t + sqrt(t * (t + 2)))
 *
 * whose rationalized form keeps the x -> 1 end exact (acosh(1) = 0
 * with the derivative 1/sqrt(2x-2) carried by the sqrt term alone).
 * Past 2^27 the sqrt is 1 to 2^-54 relative and acosh(x) is
 * log(2x) to below an ulp.
 */

#include <math.h>

#include "libm.h"

#define LN2_HI 0x1.62e42ff000000p-1	/* 33 significant bits */
#define LN2_LO -0x1.718432a1b0e26p-35

double acosh(double x)
{
	double t, hi, lo;

	if (isnan(x))
		return x;
	if (x < 1.0)
		return (x - x) / (x - x);	/* domain: nan */
	if (x == 1.0)
		return 0.0;
	if (x >= 0x1p27) {
		double s, err;

		/* log(2x) assembled as log(x) + ln 2 so 2x cannot
		   overflow near DBL_MAX. */
		__brk_log_pair(x, &hi, &lo);
		s = hi + LN2_HI;
		err = two_sum_err(hi, LN2_HI, s);
		return s + (err + lo + LN2_LO);
	}
	t = x - 1.0;
	/*
	 * log1p() routes the unbounded u through the full log
	 * machinery once it leaves the ln(1+f) core's interval; the
	 * core alone would diverge for u past 1.
	 */
	return log1p(t + sqrt(t * (t + 2.0)));
}
