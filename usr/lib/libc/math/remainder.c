/*
 * IEEE remainder and remquo.
 *
 * The reduction walks the quotient from its most significant bit
 * down: at each step the current remainder a is at least 2^k*y, so
 * subtracting 2^k*y is exact by Sterbenz and the running value
 * stays representable.  The quotient bits accumulate modulo 8 as
 * remquo needs (2^k mod 8 is zero for k >= 3), and the final
 * half-way case rounds the quotient to even exactly like the
 * to-nearest-even rule demands.
 */

#include <math.h>
#include <stddef.h>

#include "libm.h"

/*
 * Reduce a = |x| against b = |y| (both finite, b > 0, a not nan).
 * Returns the remainder in [0, b) and stores floor(a/b) mod 8.
 * Every operation is exact: the b doublings are power-of-two
 * scalings, and each a -= b step satisfies Sterbenz.
 */
static double reduce_mod(double a, double b, int *qmod8)
{
	double step = b;
	int q = 0;
	int k = 0;

	/* Grow the step toward a's binade; doubling stops before
	   overflow because 2*step <= a <= DBL_MAX. */
	while (2.0 * step <= a) {
		step *= 2.0;
		k++;
	}
	for (; k >= 0; k--) {
		if (a >= step) {
			a -= step;
			/* 2^k mod 8: 1, 2, 4, then zero for k >= 3 */
			q += k == 0 ? 1 : k == 1 ? 2 : k == 2 ? 4 : 0;
			q &= 7;
		}
		step *= 0.5;
	}
	*qmod8 = q;
	return a;
}

/*
 * Shared body: returns the IEEE remainder and, through quo, the low
 * three bits of the quotient with the sign of x/y.
 */
static double rem_body(double x, double y, int *quo)
{
	double a = fabs(x);
	double b = fabs(y);
	double half = 0.5 * b;
	double r;
	int q;

	r = reduce_mod(a, b, &q);
	if (r > half || (r == half && (q & 1))) {
		/* nearest is the other neighbor; a - b is exact by
		   Sterbenz because b/2 <= a < b here */
		r -= b;
		q = (q + 1) & 15;
	}
	/*
	 * The quotient is n mod 8; glibc reports the residue 8
	 * itself when n mod 16 lands there, so keep that spelling.
	 */
	q = q == 8 ? 8 : q & 7;
	if (quo)
		*quo = (signbit(x) != signbit(y)) ? -q : q;
	return signbit(x) ? -r : r;
}

double remainder(double x, double y)
{
	if (isnan(x) || isnan(y))
		return (x - y) / (x - y);
	if (isinf(x) || y == 0.0)
		return (x - x) / (x - x);	/* nan, like every BRK math
						   special: no errno */
	if (isinf(y))
		return x;		/* the nearest multiple is 0 */
	return rem_body(x, y, NULL);
}

double remquo(double x, double y, int *quo)
{
	int dummy;

	if (isnan(x) || isnan(y))
		return (x - y) / (x - y);
	if (isinf(x) || y == 0.0)
		return (x - x) / (x - x);
	if (isinf(y)) {
		if (quo)
			*quo = 0;
		return x;
	}
	return rem_body(x, y, quo ? quo : &dummy);
}
