/*
 * Argument reduction for sin/cos/tan: reduce |x| to
 *
 *     x = k*(pi/2) + r,   |r| <= pi/4,
 *
 * returning k mod 4 and r as a compensated pair.
 *
 * Two paths:
 *
 *  - |ax| < 2^29: a five-part split of pi/2 (scripts/gen-libm-consts.py,
 *    24 significant bits per part, aligned so k*Pi is exact for
 *    |k| < 2^29).  The first two remainders stay exactly representable
 *    (below 22*2^24 and 0.787*2^47, both inside 53 bits); the later
 *    subtractions round and each rounding is captured with two-sum.
 *    The residual beyond the split is below k*2^-96 < 2^-68.
 *
 *  - |ax| >= 2^29: fixed-point reduction.  (x*2/pi * 2^64) mod 2^128 is
 *    evaluated in 128-bit integer arithmetic from a table of the
 *    fractional bits of 2/pi (18 words = 1152 bits, enough for every
 *    finite double).  The high word carries the integer part's low
 *    bits (only its value mod 4 matters), the low word is the fraction;
 *    rounding the fraction to the nearest integer yields the octant
 *    carry and a signed remainder, whose product with pi/2 forms the
 *    compensated pair.  The truncation below the 128-bit grid costs at
 *    most one unit of the grid, so |r| inherits an error below
 *    2^-63 absolute.
 */

#include <stdint.h>

#include "libm.h"

/* 2/pi to the nearest double (k selection only). */
#define INV_PIO2 0x1.45f306dc9c883p-1

/* pi/2 five-part chain split (generated): P1 at the 2^-23 grid, P2 at
 * 2^-47, P3 at 2^-70, P4 at 2^-94, P5 the rounded residual. */
static const double PIO2_P1 = 0x1.921fb60000000p+0;
static const double PIO2_P2 = -0x1.777a5c0000000p-25;
static const double PIO2_P3 = -0x1.ee59e00000000p-50;
static const double PIO2_P4 = 0x1.8cc5180000000p-72;
static const double PIO2_P5 = -0x1.fc8f8cbb5bf6cp-97;

/* pi/2 as a hi/lo pair (hi has 20 trailing zero bits). */
#define PIO2_HI 0x1.921fb54400000p+0
#define PIO2_LO 0x1.0b4611a626331p-34

/* Fractional bits of 2/pi, 64-bit words, most significant first. */
static const uint64_t twopi_frac[18] = {
	UINT64_C(0xa2f9836e4e441529),
	UINT64_C(0xfc2757d1f534ddc0),
	UINT64_C(0xdb6295993c439041),
	UINT64_C(0xfe5163abdebbc561),
	UINT64_C(0xb7246e3a424dd2e0),
	UINT64_C(0x06492eea09d1921c),
	UINT64_C(0xfe1deb1cb129a73e),
	UINT64_C(0xe88235f52ebb4484),
	UINT64_C(0xe99c7026b45f7e41),
	UINT64_C(0x3991d639835339f4),
	UINT64_C(0x9c845f8bbdf9283b),
	UINT64_C(0x1ff897ffde05980f),
	UINT64_C(0xef2f118b5a0a6d1f),
	UINT64_C(0x6d367ecf27cb09b7),
	UINT64_C(0x4f463f669e5fea2d),
	UINT64_C(0x7527bac7ebe5f17b),
	UINT64_C(0x3d0739f78a5292ea),
	UINT64_C(0x6bfb5fb11f8d5d08),
};

static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
	uint64_t a0 = a & 0xffffffffu, a1 = a >> 32;
	uint64_t b0 = b & 0xffffffffu, b1 = b >> 32;
	uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0;
	uint64_t p11 = a1 * b1;
	uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) +
		       (p10 & 0xffffffffu);

	*lo = (p00 & 0xffffffffu) | (mid << 32);
	*hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* p[0..5] = m * vw[0..4], least significant word first.  The product
 * is below 2^373, so six words suffice and no carry can escape. */
static void mul_vw(uint64_t m, const uint64_t *vw, uint64_t *p)
{
	uint64_t hi, lo, carry, old;
	int i, j;

	for (i = 0; i < 6; i++)
		p[i] = 0;
	for (i = 0; i < 5; i++) {
		mul64(m, vw[i], &hi, &lo);
		carry = lo;
		for (j = i; carry && j < 6; j++) {
			old = p[j];
			p[j] = old + carry;
			carry = p[j] < old;
		}
		carry = hi;
		for (j = i + 1; carry && j < 6; j++) {
			old = p[j];
			p[j] = old + carry;
			carry = p[j] < old;
		}
	}
}

/* The 64-bit window of the 5-word product p starting at word w, bit b
 * (bit 64w+b of the product; words past the array read as zero). */
static uint64_t prod_word(const uint64_t *p, int w, int b)
{
	uint64_t x = w < 6 ? p[w] : 0;
	uint64_t y = (w + 1) < 6 ? p[w + 1] : 0;

	if (b == 0)
		return x;
	return (x >> b) | (y << (64 - b));
}

/* Signed 128-bit remainder, in units of 2^-128*pi/2, -> compensated r
 * pair.  The magnitude is split into a 53-bit head (exact as a
 * double), a 64-bit middle and an 11-bit tail; the head carries the
 * captured product error, the middle is rounded to a double at a
 * scale where that costs less than 2^-100 of r. */
static void rem128_to_pair(int64_t hiword, uint64_t loword,
			    double *rhi, double *rlo)
{
	int neg = hiword < 0;
	uint64_t v1 = neg ? -(uint64_t)hiword : (uint64_t)hiword;
	uint64_t v0 = loword;
	uint64_t A, B, C;
	double t1, t2, t3, e1, hi, lo;

	if (neg && loword) {
		/* two's complement of the 128-bit value */
		v1 -= 1;
		v0 = -v0;
	}
	A = v1 >> 11;			/* top 53 bits, exact as double */
	B = (v1 << 53) | (v0 >> 11);	/* next 64 bits */
	C = v0 & 0x7ff;			/* last 11 bits, exact */

	t1 = (double)A * PIO2_HI;
	e1 = two_prod_err((double)A, PIO2_HI, t1);
	t2 = (double)B * PIO2_HI;
	t3 = (double)C * PIO2_HI;	/* exact: 11+33 bits */

	/* r = (A*2^-53 + B*2^-117 + C*2^-128) * pi/2, with the pi/2
	   pair split: the head product carries its rounding in e1,
	   the head/group addition carries its own rounding into lo,
	   and the lo term applies to the raw fraction sum (units of
	   pi/2), not to the scaled products. */
	{
		double s = t1 * 0x1p-53;
		double g = t2 * 0x1p-117 + t3 * 0x1p-128;

		hi = s + g;
		lo = two_sum_err(s, g, hi) + e1 * 0x1p-53 +
		     ((double)A * 0x1p-53 + (double)B * 0x1p-117 +
		      (double)C * 0x1p-128) * PIO2_LO;
	}

	if (neg) {
		*rhi = -hi;
		*rlo = -lo;
	} else {
		*rhi = hi;
		*rlo = lo;
	}
}

hidden int __brk_rem_pio2(double ax, double *rhi, double *rlo)
{
	if (ax < 0x1p29) {
		double kd = rnd_to_int(ax * INV_PIO2);
		double r1, r2, s, h, e;
		int k = (int)kd;

		/* k*P1 and k*P2 are exact, and so are the first two
		   remainders (see header); the last three subtractions
		   round, each captured into the lo term. */
		r1 = ax - kd * PIO2_P1;
		r2 = r1 - kd * PIO2_P2;

		s = r2 - kd * PIO2_P3;
		e = two_diff_err(r2, kd * PIO2_P3, s);

		h = s;
		s = h - kd * PIO2_P4;
		e += two_diff_err(h, kd * PIO2_P4, s);

		h = s;
		s = h - kd * PIO2_P5;
		e += two_diff_err(h, kd * PIO2_P5, s);

		*rhi = s;
		*rlo = e;
		return k & 3;
	} else {
		union brk_dbits u = { ax };
		int s = (int)(u.i >> 52) - 1023 - 52;
		uint64_t m = (u.i & 0xfffffffffffffull) | (1ULL << 52);
		uint64_t vw[5], p[6], nint, fhi, flo, carry;
		int jw, sh, w, b;

		/* Window of 2/pi bits: the 53-bit significand spreads
		   every table bit over 53 product bits, so the window
		   must reach from below s-64 (the octant's fraction) up
		   to s+181 (the low word of the 128-bit fraction);
		   five 64-bit words cover the span, and words outside
		   never reach the slice. */
		jw = (s + 181 + 63) / 64 - 5;
		if (jw < 0)
			jw = 0;
		vw[0] = twopi_frac[jw + 4];	/* least significant */
		vw[1] = twopi_frac[jw + 3];
		vw[2] = twopi_frac[jw + 2];
		vw[3] = twopi_frac[jw + 1];
		vw[4] = twopi_frac[jw];		/* most significant */
		mul_vw(m, vw, p);

		/* Bits [sh, sh+128) of the product are (x*2/pi * 2^64)
		   mod 2^128: the upper word is the integer part's low
		   bits (only its value mod 4 matters), the lower word the
		   fraction.  The fraction's next 64 bits live in the
		   product just below the slice. */
		sh = 64 * (jw + 5) - s - 64;
		w = sh >> 6;
		b = sh & 63;
		fhi = prod_word(p, w, b);	/* fraction, bits 0..63 */
		if (w > 0) {
			flo = prod_word(p, w - 1, b);/* bits 64..127 */
		} else {
			/* sh < 64: only bits [0, sh) exist below */
			flo = sh ? (p[0] & ((1ULL << sh) - 1))
				    << (64 - sh) : 0;
		}
		nint = prod_word(p, w + 1, b);	/* integer part, low bits */
		carry = fhi >> 63;		/* rounding into the octant */
		/* (int64_t)fhi wraps into the signed 128-bit fraction
		   remainder, carry included */
		rem128_to_pair((int64_t)fhi, flo, rhi, rlo);
		return (int)((nint + carry) & 3);
	}
}
