/*
 * atanh(x) = 0.5 * ln((1 + x) / (1 - x)), |x| < 1.
 *
 * For |x| < 0.3 the ratio folds into log1p as 2x/(1-x), whose
 * division rounding is barely amplified there (1 + t stays above
 * 0.4).  For |x| >= 0.3 the ratio (1+x)/(1-x) is formed directly:
 * both halves are exact by Sterbenz, and the single division
 * rounds at the ratio's own scale -- at x near -1 the folded form
 * would instead round 2x/(1-x) at |x|'s scale and amplify it by
 * 1/(1+t) toward a hundred ulps.
 */

#include <math.h>

#include "libm.h"

double atanh(double x)
{
	if (isnan(x))
		return x;
	if (fabs(x) >= 1.0) {
		if (fabs(x) == 1.0)
			return copysign(1.0 / 0.0, x);
		return (x - x) / (x - x);	/* domain: nan */
	}
	/*
	 * The folded form keeps the small end exact (atanh(x) ~ x);
	 * its 2x/(1-x) division is amplified by 1/(1+t), which stays
	 * below 1.5 for |x| < 0.3.  From 0.3 up the direct ratio
	 * (1+x)/(1-x) rounds once at its own scale and the log pair
	 * takes over.
	 */
	if (fabs(x) < 0.3)
		return 0.5 * log1p(2.0 * x / (1.0 - x));
	{
		double hi, lo;

		__brk_log_pair((1.0 + x) / (1.0 - x), &hi, &lo);
		return 0.5 * (hi + lo);
	}
}
