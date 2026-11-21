/*
 * hypot(x, y) = sqrt(x^2 + y^2) without spurious overflow or
 * underflow.
 *
 * Both operands are scaled by one power of two (chosen from the
 * larger magnitude's binade, clamped so the scaling factor itself
 * stays a normal double), so the larger scaled operand sits in
 * [2^-27, 4) and neither square can overflow or go subnormal
 * before the addition.  The result is scaled back exactly.  The
 * error is the two square roundings plus the addition and the
 * hardware sqrt: under two ulps.
 */

#include <math.h>
#include <stdint.h>

#include "libm.h"

double hypot(double x, double y)
{
	double a = fabs(x), b = fabs(y);
	uint64_t ia;
	int e, k;

	if (isnan(x) || isnan(y))
		return (x + y) / (x + y);
	if (isinf(x) || isinf(y))
		return 1.0 / 0.0;
	if (a < b) {
		double t = a;
		a = b;
		b = t;
	}
	if (a == 0.0)
		return 0.0;
	ia = asu64(a);
	e = (int)(ia >> 52) - 1023;		/* a's binade */
	if ((ia << 1) == 0) {
		/* subnormal a: its binade is well below -1022 */
		e = -1023;
		while (!(ia & (1ULL << 52))) {
			ia <<= 1;
			e--;
		}
	}

	/* Scale factor 2^-k with k clamped into [-1022, 1022] so the
	   factor is a normal double; the larger scaled operand then
	   stays below 4, keeping its square in range. */
	k = e + 27;
	if (k > 1022)
		k = 1022;
	if (k < -1022)
		k = -1022;
	{
		double scale = fro64((uint64_t)(1023 - k) << 52);
		double as = a * scale;
		double bs = b * scale;
		double s = as * as + bs * bs;
		double r = sqrt(s);

		return r * fro64((uint64_t)(1023 + k) << 52);
	}
}
