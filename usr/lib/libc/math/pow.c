/*
 * pow(x, y).
 *
 * x^y = e^(y*ln x) for positive x.  The amplification of the log error
 * by a large |y| is the classic trap, so ln(x) is taken as a
 * compensated pair (__brk_log_pair), the product y*ln(x) is also formed
 * as a pair (the hi product's rounding is captured through an exact
 * split of the moderate log term), and the pair feeds the compensated
 * exponential kernel __brk_exp_pair.
 *
 * For |y| up to a few hundred the result stays within ~1 ulp; beyond
 * that the unavoidable y*(log error) term grows, but finite results then
 * require |ln x| to shrink correspondingly, and the pair error shrinks
 * with it, so the degradation is far slower than a naive y*log(x)
 * formulation would show.
 *
 * Negative x with an integer exponent uses |x| and applies the sign;
 * the zeros, infinities and NaN cases follow the C99 table.
 */

#include <stdint.h>

#include "libm.h"

/* C99 exponent classification: 0 = not an integer, 1 = odd, 2 = even. */
static int y_kind(double y)
{
	union brk_dbits u = { y };
	int e = (int)(u.i >> 52 & 0x7ff) - 1023;

	if (e >= 53)
		return 2;		/* every such integer is even */
	if (e < 0)
		return 0;		/* |y| < 1: not an integer */
	/* fraction bits below the ones place must vanish */
	if (u.i & ((1ULL << (52 - e)) - 1))
		return 0;
	return (int)(u.i >> (52 - e)) & 1 ? 1 : 2;
}

double pow(double x, double y)
{
	uint64_t ux = asu64(x), uy = asu64(y);
	double ax, lh, ll, th, tl, r;
	int yk;

	if (uy << 1 == 0)
		return 1.0;			/* pow(x, +-0) = 1 */
	if (ux == asu64(1.0))
		return 1.0;			/* pow(1, y) = 1, even nan */
	if (uy << 1 > 0xffe0000000000000ull)
		return y + y;			/* pow(x, nan) = nan */
	if (ux << 1 == 0xffe0000000000000ull) {
		/* x = +/-inf */
		if (uy >> 63) {
			/* y < 0: signed zeros */
			if (y_kind(y) == 1 && ux >> 63)
				return -0.0;
			return 0.0;
		}
		if (y_kind(y) == 1 && ux >> 63)
			return -1.0 / 0.0;	/* odd y, -inf */
		return y / y;			/* +inf */
	}
	if (ux << 1 > 0xffe0000000000000ull)
		return x + x;			/* pow(nan, y) = nan */
	if (uy << 1 == 0xffe0000000000000ull) {
		/* y = +/-inf */
		ax = fro64(ux & ~(1ULL << 63));
		if (ax == 1.0)
			return 1.0;		/* includes pow(-1, inf) */
		if (uy >> 63)
			return ax < 1.0 ? y / y : 0.0;
		return ax < 1.0 ? 0.0 : y / y;
	}

	/* x and y are finite, y != 0. */
	ax = fro64(ux & ~(1ULL << 63));
	if (ax == 0.0) {
		/* pow(+-0, y) */
		if (uy >> 63) {
			if (y_kind(y) == 1 && ux >> 63)
				return -1.0 / 0.0;	/* -0 base, odd */
			return 1.0 / 0.0;
		}
		if (y_kind(y) == 1 && ux >> 63)
			return x;			/* -0, odd y */
		return 0.0;
	}

	yk = 2;
	if (ux >> 63) {
		yk = y_kind(y);
		if (yk == 0)
			return (x - x) / (x - x);	/* not an integer */
		x = ax;				/* work on |x| */
	}

	__brk_log_pair(x, &lh, &ll);
	th = y * lh;
	if (th - th != 0.0) {
		/* The product overflowed or vanished: the exact image is
		   already 0 or inf and no lo term can change that. */
		tl = 0.0;
	} else {
		/* Capture the hi product's rounding through a split of
		   the moderate log operand, then fold in the log's lo. */
		tl = two_prod_err(lh, y, th) + y * ll;
	}
	r = __brk_exp_pair(th, tl);
	if (yk == 1)
		r = -r;			/* negative base, odd exponent */
	return r;
}
