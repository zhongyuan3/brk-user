/*
 * asin(x), and the core polynomial shared with acos.
 *
 * For |x| <= 1/2: asin(x) = x + x^3*A(x^2), a degree-14 interpolation
 * on [0, 1/4] (scripts/gen-libm-consts.py) evaluated in factored form.
 *
 * For 1/2 < |x| < 1: the half-angle complement
 *
 *     asin(x) = pi/2 - 2*asin(sqrt((1-|x|)/2)),
 *
 * where (1-|x|)/2 is exact by Sterbenz, the sqrt rounds once, and the
 * small complementary value keeps every error term at the scale of the
 * correction rather than of pi/2.
 */

#include <stdint.h>

#include "libm.h"

#define PIO2_HI 0x1.921fb54400000p+0
#define PIO2_LO 0x1.0b4611a626331p-34

/* asin(t) = t + t^3*A(t^2) for |t| <= 1/2; generated fit. */
static const double asin_c[15] = {
	0x1.5555555555555p-3,
	0x1.333333333332dp-4,
	0x1.6db6db6db7c7fp-5,
	0x1.f1c71c70ee54ap-6,
	0x1.6e8ba31c9dc09p-6,
	0x1.1c4ebd7735bf2p-6,
	0x1.c99afe79266f3p-7,
	0x1.7a70c2a695fe0p-7,
	0x1.40e38d543d8d2p-7,
	0x1.0a9849ba5937fp-7,
	0x1.2031dbbbbad45p-7,
	0x1.8f5a75293b096p-12,
	0x1.7184808eff5eep-6,
	-0x1.898d77b6dddb2p-6,
	0x1.f74bdb0d4b5bep-6,
};

hidden double __brk_asin_poly(double t)
{
	double w = t * t;
	double c = asin_c[14];
	int i;

	for (i = 13; i >= 0; i--)
		c = c * w + asin_c[i];
	return t + t * w * c;
}

double asin(double x)
{
	uint64_t xs = asu64(x) & (1ULL << 63);
	double ax = fro64(asu64(x) & ~(1ULL << 63));
	double t, v, s;

	if ((asu64(ax) << 1) >= 0xffe0000000000000ull)
		return (x - x) / (x - x);	/* nan */
	if (ax > 1.0)
		return (x - x) / (x - x);	/* |x| > 1 */
	if (ax == 1.0) {
		v = 1.5707963267948966;		/* asin(+-1) = +-pi/2 */
	} else if (ax <= 0.5) {
		v = __brk_asin_poly(ax);
	} else {
		t = sqrt((1.0 - ax) * 0.5);
		v = 2.0 * __brk_asin_poly(t);
		s = PIO2_HI - v;
		v = s + (PIO2_HI - s - v + PIO2_LO);
	}
	return xs ? -v : v;
}
