/*
 * acos(x).
 *
 * Companion of asin(x), avoiding the pi/2 - asin(x) cancellation near
 * x = +-1 by using the half-angle identities directly:
 *
 *     acos(x) = 2*asin(sqrt((1-x)/2))          for 1/2 <= x <= 1
 *     acos(x) = pi - 2*asin(sqrt((1+x)/2))     for -1 <= x < -1/2
 *     acos(x) = pi/2 - asin(x)                 for |x| <= 1/2 (no
 *                                               cancellation there)
 */

#include <stdint.h>

#include "libm.h"

#define PIO2_HI 0x1.921fb54400000p+0
#define PIO2_LO 0x1.0b4611a626331p-34
#define PI_HI 0x1.921fb54400000p+1
#define PI_LO 0x1.0b4611a626331p-33

double acos(double x)
{
	double ax = fro64(asu64(x) & ~(1ULL << 63));
	double t, v, s;

	if ((asu64(ax) << 1) >= 0xffe0000000000000ull)
		return (x - x) / (x - x);	/* nan */
	if (ax > 1.0)
		return (x - x) / (x - x);	/* |x| > 1 */
	if (x == 1.0)
		return 0.0;			/* acos(1) = +0 */
	if (x == -1.0)
		return 3.141592653589793;	/* acos(-1) = pi */

	if (ax <= 0.5) {
		/* no cancellation: the result is at least pi/3 away from
		   both pi/2 and pi; the core is odd, so x keeps its sign */
		v = __brk_asin_poly(x);
		s = PIO2_HI - v;
		return s + (PIO2_HI - s - v + PIO2_LO);
	}

	if (!(asu64(x) >> 63)) {
		/* x in (1/2, 1): acos(x) = 2*asin(sqrt((1-x)/2)) */
		t = sqrt((1.0 - x) * 0.5);
		return 2.0 * __brk_asin_poly(t);
	}

	/* x in [-1, -1/2): acos(x) = pi - 2*asin(sqrt((1+x)/2)) */
	t = sqrt((1.0 + x) * 0.5);
	v = 2.0 * __brk_asin_poly(t);
	s = PI_HI - v;				/* v <= pi: Sterbenz */
	return s + (PI_HI - s - v + PI_LO);
}
