/*
 * cbrt(x) = x^(1/3), sign-symmetric and exact at cubes of powers
 * of two.
 *
 * cbrt(|x|) = e^(ln(|x|)/3) through the compensated log and exp
 * pair cores.  The /3 is the delicate step: the quotient of the
 * log pair's head is captured exactly with an FMA residual
 * (r = lhi - 3*q has no rounding, the classic fused-remainder
 * trick), so the exponent fed to the exp pair carries the log
 * pair's full precision and the result stays near half an ulp.
 */

#include <math.h>

#include "libm.h"

double cbrt(double x)
{
	double a = fabs(x);
	double lhi, llo, q, r, hi, elo;

	if (isnan(x))
		return x;
	if ((asu64(x) << 1) == 0)
		return x;			/* +-0, inf: passthrough */

	__brk_log_pair(a, &lhi, &llo);
	q = lhi / 3.0;
	r = __builtin_fma(-3.0, q, lhi);	/* exact: lhi - 3*q */
	__brk_exp_full(q, r / 3.0 + llo / 3.0, &hi, &elo);
	return copysign(hi + elo, x);
}
