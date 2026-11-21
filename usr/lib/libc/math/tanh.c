/*
 * tanh(x) = (e^(2x) - 1) / (e^(2x) + 1).
 *
 * For |x| <= 0.5 the identity t/(t+2) with t = expm1(2|x|) would
 * amplify expm1's absolute error by 1/t, so the correction series
 * is fitted directly instead:
 *
 *     tanh(x) = x + x^3 * H(x^2),  H from gen-libm-consts.py,
 *
 * whose Horner roundings are suppressed by x^3 and whose truncation
 * stays below 2^-58 relative there.  Beyond 0.5 the t/(t+2) form
 * has t >= e-1 and loses nothing; past 22 the result is 1 - 2^-64
 * or closer, which rounds to exactly 1.0.
 */

#include <math.h>

#include "libm.h"

/* tanh(x) = x + x^3 * H(w), w = x^2 on [0, 0.25];
 * degree 14, fitted by scripts/gen-libm-consts.py. */
static const double tanh_h[15] = {
	-0x1.5555555555555p-2, 0x1.1111111111111p-3,
	-0x1.ba1ba1ba1ba1cp-5, 0x1.664f4882c10f3p-6,
	-0x1.226e355e6bf10p-7, 0x1.d6d3d0e13a409p-9,
	-0x1.7da3644d13160p-10, 0x1.355823c346dcbp-11,
	-0x1.f57d5435c2e95p-13, 0x1.967bc33eb255fp-14,
	-0x1.4960b5dd88c23p-15, 0x1.0a12681b3df86p-16,
	-0x1.a458beda17165p-18, 0x1.28e2506ad7afap-19,
	-0x1.11797bad9f395p-21,
};

double tanh(double x)
{
	double a = fabs(x);

	if (isnan(x))
		return x;
	if (a > 22.0)
		return copysign(1.0, x);
	if (a < 0.5) {
		double w = x * x;
		double h;
		int i;

		h = tanh_h[14];
		for (i = 13; i >= 0; i--)
			h = h * w + tanh_h[i];
		return x + x * x * x * h;
	}
	{
		double t = expm1(2.0 * a);

		return copysign(t / (t + 2.0), x);
	}
}
