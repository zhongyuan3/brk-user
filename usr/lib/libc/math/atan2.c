/*
 * atan2(y, x).
 *
 * The C99 quadrant table for the zeros and infinities, then the core
 * from atan.c applied to the smaller of |y/x| and |x/y|:
 *
 *   |y| <= |x|:  atan2 = atan(|y/x|)        (pi - it when x < 0)
 *   |y| >  |x|:  atan2 = pi/2 - atan(|x/y|) (pi/2 + it when x < 0)
 *
 * with the result carrying the sign of y.
 */

#include <stdint.h>

#include "libm.h"

#define PIO2_HI 0x1.921fb54400000p+0
#define PIO2_LO 0x1.0b4611a626331p-34
#define PI_HI 0x1.921fb54400000p+1
#define PI_LO 0x1.0b4611a626331p-33

/* pi/2 as one correctly rounded value */
#define PIO2 1.5707963267948966
#define PI 3.141592653589793

static double pio4(void)
{
	/* pi/4 = (pi/2) * 1/2 exactly in the pair */
	return PIO2_HI * 0.5 + PIO2_LO * 0.5;
}

static double three_pio4(void)
{
	/* 3*pi/4 = pi - pi/4 with the rounding captured */
	double p4 = PIO2_HI * 0.5;
	double s = PI_HI - p4;

	return s + (PI_HI - s - p4 + PI_LO - PIO2_LO * 0.5);
}

double atan2(double y, double x)
{
	uint64_t uy = asu64(y), ux = asu64(x);
	uint64_t ysign = uy & (1ULL << 63), xsign = ux & (1ULL << 63);
	double ay, ax, q, v, s;

	/* nan in either argument */
	if ((uy << 1) > 0xffe0000000000000ull ||
	    (ux << 1) > 0xffe0000000000000ull)
		return x + y;

	ay = fro64(uy & ~(1ULL << 63));
	ax = fro64(ux & ~(1ULL << 63));

	/* infinities */
	if ((uy << 1) == 0xffe0000000000000ull) {
		if ((ux << 1) == 0xffe0000000000000ull) {
			double lim = xsign ? three_pio4() : pio4();

			return ysign ? -lim : lim;
		}
		return ysign ? -PIO2 : PIO2;
	}
	if ((ux << 1) == 0xffe0000000000000ull) {
		if (!xsign)
			return ysign ? -0.0 : 0.0;
		return ysign ? -PI : PI;
	}

	/* zeros */
	if (ay == 0.0) {
		if (!xsign && ax != 0.0)
			return ysign ? -0.0 : 0.0;
		if (xsign)
			return ysign ? -PI : PI;
		/* x is +0: atan2(+-0, +0) = +-0 */
		return ysign ? -0.0 : 0.0;
	}
	if (ax == 0.0)
		return ysign ? -PIO2 : PIO2;

	/* main quadrants */
	if (ay > ax) {
		q = ax / ay;
		v = __brk_atan_core(q);
		s = PIO2_HI - v;
		v = s + (PIO2_HI - s - v + PIO2_LO);
		if (xsign) {
			s = PI_HI - v;		/* v <= pi/2: Sterbenz */
			v = s + (PI_HI - s - v + PI_LO);
		}
	} else {
		q = ay / ax;
		v = __brk_atan_core(q);
		if (xsign) {
			s = PI_HI - v;
			v = s + (PI_HI - s - v + PI_LO);
		}
	}
	return ysign ? -v : v;
}
