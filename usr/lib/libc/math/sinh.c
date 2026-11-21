/*
 * sinh(x) = (e^x - e^-x) / 2.
 *
 * For |x| <= 22 the expm1 form avoids overflow:
 *
 *     sinh(x) = sign(x) * (t + t/(t+1)) / 2,  t = expm1(|x|),
 *
 * which stays accurate down to |x| where sinh(x) ~ x (t ~ x, and
 * the t/(t+1) term supplies the e^-x half).  Beyond 22 the e^-|x|
 * half is below 2^-54 relative and sinh collapses to e^|x|/2
 * through the exp pair, whose overflow behavior is inherited.
 */

#include <math.h>

#include "libm.h"

double sinh(double x)
{
	double t;

	if (isnan(x))
		return x;
	if (fabs(x) > 22.0) {
		double hi, elo;

		if (fabs(x) >= 1024.0)
			return copysign(1.0 / 0.0, x);
		/*
		 * sinh = e^x/2 * (1 - e^-2x); the correction is below
		 * 2^-54 relative here, so the result is e^(x - ln 2).
		 * Folding the -ln 2 into the exp pair's lo term avoids
		 * the x - ln 2 subtraction, which would round at x's
		 * own scale and lose thousands of ulps, and it keeps
		 * the overflow boundary (sinh finite where e^x is not)
		 * exact.
		 */
		__brk_exp_full(fabs(x), -0.6931471805599452862267639899,
			       &hi, &elo);
		return copysign(hi + elo, x);
	}
	t = expm1(fabs(x));
	return copysign(0.5 * (t + t / (t + 1.0)), x);
}
