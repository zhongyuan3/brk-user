/*
 * expm1(x) = e^x - 1, accurate for tiny x.
 *
 * For |x| <= ln(2)/2 the Taylor series is evaluated in the factored
 * form x + x^2*Q(x) with exact rational coefficients: the Q Horner
 * roundings are suppressed by x^2, and the truncation at x^14/14!
 * stays below 2^-63 absolute at the interval ends.  Beyond that,
 * exp(x) - 1 loses at most one extra rounding: the subtraction is
 * exact by Sterbenz for the results in [0.5, 2] that matter, and
 * the exp core itself is good to 0.6 ulp.  Overflow and underflow
 * ride the exp core's exact power-of-two scalings.
 */

#include <math.h>

#include "libm.h"

/* ln(2)/2 */
#define LN2_OVER2 0.34657359027997265470861606

double expm1(double x)
{
	static const double q_c[13] = {
		1.0 / 2.0, 1.0 / 6.0, 1.0 / 24.0, 1.0 / 120.0,
		1.0 / 720.0, 1.0 / 5040.0, 1.0 / 40320.0,
		1.0 / 362880.0, 1.0 / 3628800.0, 1.0 / 39916800.0,
		1.0 / 479001600.0, 1.0 / 6227020800.0,
		1.0 / 87178291200.0,
	};

	if (isnan(x))
		return x;
	if (x > 1e300)
		return 1.0 / 0.0;		/* e^huge = +inf */
	if (x < -1e300)
		return -1.0;			/* e^-huge underflows to 0 */
	if (x > -LN2_OVER2 && x < LN2_OVER2) {
		double q;
		int i;

		q = q_c[12];
		for (i = 11; i >= 0; i--)
			q = q * x + q_c[i];
		return x + x * x * q;
	}
/*
	 * Beyond the series interval: e^x - 1 through the exp pair,
	 * so the small result end keeps the exp core's accuracy.  The
	 * subtraction from hi is exact by Sterbenz for hi in [0.5, 2]
	 * (x in [-ln 2, ln 2]); outside that it is a single rounding
	 * at a scale where the result is no longer delicate.
	 */
	{
		double hi, elo;

		__brk_exp_full(x, 0.0, &hi, &elo);
		return (hi - 1.0) + elo;
	}
}
