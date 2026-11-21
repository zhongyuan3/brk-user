/*
 * exp2(x) = 2^x.
 *
 * The argument splits as x = k + r with k = round-to-nearest(x),
 * |r| <= 0.5, and 2^x = 2^k * e^(r*ln2).  Because r is small, the
 * r*ln2 product is captured with Dekker's two-product and handed to
 * the exp pair core, whose own error stays near half an ulp; the
 * power-of-two scaling is exact outside the overflow and subnormal
 * fringes.  A direct e^(x*ln2) would instead round x*ln2 at the
 * full magnitude of x and lose thousands of ulps for large |x|,
 * which is why the k rounding comes first.
 */

#include <math.h>

#include "libm.h"

#define LN2_HI 0x1.62e42ff000000p-1	/* 33 significant bits */
#define LN2_LO -0x1.718432a1b0e26p-35

double exp2(double x)
{
	double kd, r, p, e, hi, elo;
	int k;

	if (isnan(x))
		return x;
	if (x >= 1024.0)
		return 1.0 / 0.0;		/* 2^1024 overflows */
	if (x < -1075.0)
		return 0.0;			/* below the last subnormal */
	kd = rnd_to_int(x);
	k = (int)kd;
	r = x - kd;				/* exact: |r| <= 0.5 */

	p = r * LN2_HI;
	e = two_prod_err(r, LN2_HI, p);
	__brk_exp_full(p, r * LN2_LO + e, &hi, &elo);
	hi = __brk_scale_pow2(hi, k);
	elo = __brk_scale_pow2(elo, k);
	if ((asu64(hi) << 1) == 0xffe0000000000000ull)
		elo = 0.0;			/* overflow edge: no folding */
	return hi + elo;
}
