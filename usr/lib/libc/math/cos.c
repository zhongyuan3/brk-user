/*
 * cos(x).
 *
 * Same reduction and cores as sin(x); only the octant mapping differs.
 */

#include "libm.h"

/* sin(r) = r + r^3*Cs(r^2); Taylor coefficients -1/3! .. +1/17! */
static const double sin_c[8] = {
	-1.0 / 6.0, 1.0 / 120.0, -1.0 / 5040.0, 1.0 / 362880.0,
	-1.0 / 39916800.0, 1.0 / 6227020800.0, -1.0 / 1307674368000.0,
	1.0 / 355687414096000.0,
};

/* cos(r) = 1 + r^2*Cc(r^2); Taylor coefficients -1/2! .. -1/18! */
static const double cos_c[9] = {
	-0.5, 1.0 / 24.0, -1.0 / 720.0, 1.0 / 40320.0,
	-1.0 / 3628800.0, 1.0 / 479001600.0, -1.0 / 87178291200.0,
	1.0 / 20922789888000.0, -1.0 / 6402373705728000.0,
};

static double sin_core(double r)
{
	double w = r * r;
	double c = sin_c[7];
	int i;

	for (i = 6; i >= 0; i--)
		c = c * w + sin_c[i];
	return r + r * w * c;
}

static double cos_core(double r)
{
	double w = r * r;
	double c = cos_c[8];
	int i;

	for (i = 7; i >= 0; i--)
		c = c * w + cos_c[i];
	return 1.0 + w * c;
}

double cos(double x)
{
	double rh, rl, s, c, v;
	int oct;

	if ((asu64(x) << 1) >= 0xffe0000000000000ull) {
		if ((asu64(x) << 1) == 0xffe0000000000000ull)
			return (x - x) / (x - x);	/* cos(inf) = nan */
		return x;				/* cos(nan) = nan */
	}

	oct = __brk_rem_pio2(fro64(asu64(x) & ~(1ULL << 63)), &rh, &rl);
	s = sin_core(rh);
	c = cos_core(rh);
	{
		double s0 = s;

		s += rl * c;
		c -= rl * s0;
	}

	switch (oct) {
	case 0:
		v = c;
		break;
	case 1:
		v = -s;
		break;
	case 2:
		v = -c;
		break;
	default:
		v = s;
		break;
	}
	return v;
}
