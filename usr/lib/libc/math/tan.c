/*
 * tan(x).
 *
 * Reduction as in sin.c; the core is a degree-16 minimax-style
 * interpolation in w = r^2 on [0, (pi/4)^2] (coefficients from
 * scripts/gen-libm-consts.py),
 *
 *     tan(r) = r + r^3 * T(w),
 *
 * and the octant picks between t and -1/t (tan has period pi, and its
 * value at the odd multiples of pi/2 is -cot).
 */

#include "libm.h"

static const double tan_c[17] = {
	0x1.5555555555555p-2,
	0x1.1111111111110p-3,
	0x1.ba1ba1ba1bc09p-5,
	0x1.664f4882b24afp-6,
	0x1.226e35622425cp-7,
	0x1.d6d3cfbf1a15ep-9,
	0x1.7da3816c2c352p-10,
	0x1.35561ee6aaabap-11,
	0x1.f5b10588249a9p-13,
	0x1.949cfc61c7108p-14,
	0x1.5677978a5c6bep-15,
	0x1.91536255dae2dp-17,
	0x1.d24e2f91c679fp-17,
	-0x1.f488308cdbd4bp-18,
	0x1.5eb0f15cb3adcp-17,
	-0x1.5689e9cdee1e0p-18,
	0x1.ed9a4062964a7p-20,
};

double tan(double x)
{
	uint64_t xs = asu64(x) & (1ULL << 63);
	double ax = fro64(asu64(x) & ~(1ULL << 63));
	double rh, rl, w, t, c, v;
	int oct, i;

	if ((asu64(ax) << 1) >= 0xffe0000000000000ull)
		return (x - x) / (x - x);	/* inf or nan */

	oct = __brk_rem_pio2(ax, &rh, &rl);
	w = rh * rh;
	c = tan_c[16];
	for (i = 15; i >= 0; i--)
		c = c * w + tan_c[i];
	t = rh + rh * w * c;
	t += rl * (1.0 + t * t);		/* residual via derivative */

	if (oct & 1)
		v = -1.0 / t;
	else
		v = t;
	return xs ? -v : v;
}
