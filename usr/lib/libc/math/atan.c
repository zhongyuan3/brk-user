/*
 * atan(x) and the zoned core shared with atan2.
 *
 * Core (0 <= u <= 1):
 *   - u <= 1/2:  atan(u) = u + u^3*B(u^2), a degree-12 interpolation on
 *     [0, 1/4] (scripts/gen-libm-consts.py), factored like the other
 *     cores.
 *   - u > 1/2:   atan(u) = pi/6 + atan((u - 1/sqrt3)/(1 + u/sqrt3));
 *     the reduced argument lands back in [-1/13, 1/4]*... inside the
 *     polynomial range, and 1/sqrt3 is a hi/lo pair whose hi part is
 *     exact to subtract (Sterbenz) for u > 1/2.
 *
 * atan itself maps |x| > 1 through atan(1/x) and the pi/2 complement.
 */

#include <stdint.h>

#include "libm.h"

#define PIO2_HI 0x1.921fb54400000p+0
#define PIO2_LO 0x1.0b4611a626331p-34
#define PIO6_HI 0x1.0c15238300000p-1	/* pi/6, 33 significant bits */
#define PIO6_LO -0x1.464d3dcd22669p-36
#define T6_HI 0x1.279a745900000p-1	/* 1/sqrt(3), 33 significant bits */
#define T6_LO 0x1.98e2690c7c0f2p-40

static const double atan_c[13] = {
	-0x1.5555555555555p-2,
	0x1.99999999998e1p-3,
	-0x1.2492492488296p-3,
	0x1.c71c71c01b24cp-4,
	-0x1.745d16026efd7p-4,
	0x1.3b138e6372dcap-4,
	-0x1.110ea50256abfp-4,
	0x1.e1a7b2562efc5p-5,
	-0x1.ad44da04adea3p-5,
	0x1.7b20b751c966ap-5,
	-0x1.37d750492dd9bp-5,
	0x1.99b4e0c058248p-6,
	-0x1.2dfbaab9786efp-7,
};

static double atan_poly(double u)
{
	double w = u * u;
	double c = atan_c[12];
	int i;

	for (i = 11; i >= 0; i--)
		c = c * w + atan_c[i];
	return u + u * w * c;
}

hidden double __brk_atan_core(double u)
{
	double num, den, v, s;

	if (u <= 0.5)
		return atan_poly(u);

	/* atan(u) = pi/6 + atan((u - t6)/(1 + t6*u)) */
	num = (u - T6_HI) - T6_LO;		/* first part is Sterbenz */
	den = 1.0 + u * T6_HI + u * T6_LO;
	v = atan_poly(num / den);
	s = PIO6_HI + v;
	return s + (PIO6_HI - s + v + PIO6_LO);
}

double atan(double x)
{
	uint64_t xs = asu64(x) & (1ULL << 63);
	double ax = fro64(asu64(x) & ~(1ULL << 63));
	double v, s;

	if ((asu64(ax) << 1) > 0xffe0000000000000ull)
		return (x - x) / (x - x);	/* nan */
	if ((asu64(ax) << 1) == 0xffe0000000000000ull) {
		/* atan(+-inf) = +-pi/2 */
		return xs ? -1.5707963267948966 : 1.5707963267948966;
	}

	if (ax > 1.0) {
		v = __brk_atan_core(1.0 / ax);
		s = PIO2_HI - v;		/* v <= pi/4: Sterbenz */
		v = s + (PIO2_HI - s - v + PIO2_LO);
	} else {
		v = __brk_atan_core(ax);
	}
	return xs ? -v : v;
}
