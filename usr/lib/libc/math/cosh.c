/*
 * cosh(x) = (e^x + e^-x) / 2.
 *
 * For |x| <= 22 the expm1 form avoids overflow:
 *
 *     cosh(x) = 1 + t*t/(2*(t+1)),  t = expm1(|x|),
 *
 * whose leading 1 keeps the small-|x| end exact (cosh(0) = 1 and
 * the x^2/2 term rides on t).  Beyond 22, cosh collapses to
 * e^|x|/2 through the exp pair.
 */

#include <math.h>

#include "libm.h"

double cosh(double x)
{
	double t;

	if (isnan(x))
		return x;
	if (fabs(x) > 22.0) {
		double hi, elo;

		if (fabs(x) >= 1024.0)
			return 1.0 / 0.0;
		/*
		 * cosh = e^x/2 * (1 + e^-2x); the correction is below
		 * 2^-54 relative here, and folding the -ln 2 into the
		 * exp pair's lo term keeps the overflow boundary exact
		 * (see sinh()).
		 */
		__brk_exp_full(fabs(x), -0.6931471805599452862267639899,
			       &hi, &elo);
		return hi + elo;
	}
	t = expm1(fabs(x));
	return 1.0 + t * t / (2.0 * (t + 1.0));
}
