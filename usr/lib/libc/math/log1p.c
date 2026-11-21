/*
 * log1p(x) = log(1 + x), accurate for tiny x.
 *
 * For |x| < 0.5 the ln(1+f) pair core runs on f = x directly, so
 * nothing is lost to forming 1 + x (which would be the whole point
 * of the function: log1p(1e-20) is 1e-20, not log(1) = 0).  The
 * atanh branch of that core reaches down to f = -0.5.  Beyond the
 * interval, 1 + x is formed once: its relative error is at most one
 * rounding, and the log pair turns it into well under an ulp of the
 * result.
 */

#include <math.h>

#include "libm.h"

double log1p(double x)
{
	double hi, lo;

	if (isnan(x))
		return x;
	if (x == -1.0)
		return -1.0 / 0.0;		/* log(0) = -inf */
	if (x < -1.0)
		return (x - x) / (x - x);	/* log(negative) = nan */
	if (x > -0.5 && x < 1.0) {
		/* The atanh branch of the pair core covers this whole
		   interval (z stays within +-1/3, where the 17-term
		   truncation keeps 2^-59), so 1 + x is never formed
		   while it could still cost precision.  Below -0.5 the
		   z series would diverge, so the m-path takes over. */
		__brk_ln1p_pair(x, &hi, &lo);
		return hi + lo;
	}
	if ((asu64(x) << 1) >= 0xffe0000000000000ull)
		return x;			/* +inf */
	__brk_log_pair(1.0 + x, &hi, &lo);
	return hi + lo;
}
