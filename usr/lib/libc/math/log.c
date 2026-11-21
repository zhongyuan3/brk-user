/*
 * log(x) and the hi/lo natural log pair shared with pow.
 *
 * Method: x = 2^e * m with m in [2^-0.5, 2^0.5) (the sqrt(2)-centered
 * split, so x near 1 always lands at e = 0 and no cancellation can
 * occur), then with f = m - 1 (exact by Sterbenz):
 *
 *   - |f| <= 2^-5:  ln(m) = f + f*v(f), a 13-term Taylor correction in
 *     exact rational coefficients (the series is evaluated in factored
 *     form, so its rounding error is suppressed by f^2 and stays below
 *     2^-60 absolute).
 *   - otherwise:     ln(m) = 2z + 2z*C(z^2), z = f/(2+f): the atanh
 *     form with odd terms through z^23.
 *
 * The result is assembled as a compensated pair: the e*ln2 term uses a
 * hi part with 20 trailing zero bits so e*hi is exact, and every
 * rounding along the way is captured with two-sum.  The pair error is
 * below 2^-58 absolute; log() itself collapses it with one final
 * rounding.
 */

#include <stdint.h>

#include "libm.h"

#define LN2_HI 0x1.62e42ff000000p-1	/* 33 significant bits */
#define LN2_LO -0x1.718432a1b0e26p-35
#define LOG10E_HI 0x1.bcb7b15200000p-2	/* 33 significant bits */
#define LOG10E_LO 0x1.b9438ca9aadd5p-36
#define LOG2E_HI 0x1.7154765300000p+0	/* 33 significant bits */
#define LOG2E_LO -0x1.1f407a220bc01p-34

/* Mantissa at or above this value means m >= sqrt(2): halve it (an exact
 * exponent adjustment -- the fraction bits are untouched). */
#define SQRT2_FRAC 0x0006a09e667f3bcdull

hidden void __brk_ln1p_pair(double f, double *hi, double *lo)
{
	if (f > -0.03125 && f < 0.03125) {
		/* ln(1+f) = f + f^2*(-1/2 + f/3 - ... + f^11/13):
		   the correction is suppressed by f^2, so the Horner
		   rounding stays below 2^-60 absolute. */
		static const double v_c[12] = {
			-0.5, 1.0 / 3.0, -0.25, 0.2,
			-1.0 / 6.0, 1.0 / 7.0, -0.125, 1.0 / 9.0,
			-0.1, 1.0 / 11.0, -1.0 / 12.0, 1.0 / 13.0,
		};
		double v;
		int i;

		v = v_c[11];
		for (i = 10; i >= 0; i--)
			v = v * f + v_c[i];

		*hi = f;
		*lo = f * f * v;
		return;
	}

	/* ln(1+f) = 2z + 2z*C(w), w = z*z, z = f/(2+f),
	   C = w/3 + w^2/5 + ... + w^16/33.
	   The series runs one term per w-power further than log()
	   needs (its |f| stays below 0.414): log1p() calls this with
	   |f| up to 0.5, where z reaches -1/3 and w 1/9, and the
	   17-term truncation stays below 2^-59 absolute there.  The
	   division z and the product den*z both round, and neither
	   error is suppressed by w, so z is formed as a compensated
	   pair: pow amplifies the pair error by |y| and would lose
	   several ulps at moderate exponents without it. */
	{
		static const double c_c[17] = {
			1.0 / 3.0, 1.0 / 5.0, 1.0 / 7.0, 1.0 / 9.0,
			1.0 / 11.0, 1.0 / 13.0, 1.0 / 15.0, 1.0 / 17.0,
			1.0 / 19.0, 1.0 / 21.0, 1.0 / 23.0, 1.0 / 25.0,
			1.0 / 27.0, 1.0 / 29.0, 1.0 / 31.0, 1.0 / 33.0,
			1.0 / 35.0,
		};
		double den = 2.0 + f;	/* rounds: 2+f needs up to 56 bits */
		double den_lo = two_sum_err(2.0, f, den);
		double zh = f / den;
		double prod = den * zh;
		double perr = two_prod_err(den, zh, prod);
		double zl = (f - prod - perr) / den;	/* f/den - zh exactly */
		/* den_lo shifts the true quotient z = f/(2+f) away from
		   f/den by -f*den_lo/den^2; pow amplifies the pair error
		   by |y|, so without this the pair is only good to
		   ~2^-53 of z. */
		zl -= f * den_lo / (den * den);	/* z = zh + zl */
		{
			double w = zh * zh;
			double rest, t, e, c;
			int i;

			/* Horner through 1/5, then split the last
			   multiply and the 1/3 add: both roundings are
			   captured, because they are the only ones not
			   suppressed by w. */
			rest = c_c[16];
			for (i = 15; i >= 1; i--)
				rest = rest * w + c_c[i];
			t = rest * w;
			e = two_prod_err(rest, w, t);
			c = t + c_c[0];
			e += two_sum_err(t, c_c[0], c);/* C = w*(c+e) exactly */

			*hi = 2.0 * zh;		/* exact scaling of head */
			*lo = 2.0 * zh * w * c + 2.0 * zh * w * e +
			      2.0 * zl * (1.0 + w * c);
		}
	}
}

hidden void __brk_log_pair(double x, double *hi, double *lo)
{
	union brk_dbits u = { x };
	double m, f, lnh, lnl;
	int e, half;

	e = (int)(u.i >> 52) - 1023;
	if (e == -1023) {
		/* subnormal: one exact scaling lands in the normals */
		x *= 0x1p64;
		u.f = x;
		e = (int)(u.i >> 52) - 1023 - 64;
	}
	half = (u.i & 0xfffffffffffffull) >= SQRT2_FRAC;
	if (half)
		e++;
	/* m in [2^-0.5, 2^0.5): same fraction, shifted exponent field */
	u.i = (u.i & 0x800fffffffffffffull) |
	      ((uint64_t)(1023 - half) << 52);
	m = u.f;
	f = m - 1.0;			/* exact */

	__brk_ln1p_pair(f, &lnh, &lnl);
	/* Combine: e*ln2 + ln(m), with two-sum capturing each rounding. */
	{
		double eh = e * LN2_HI;	/* exact */
		double s = eh + lnh;
		double err = two_sum_err(eh, lnh, s);

		*hi = s;
		*lo = err + lnl + e * LN2_LO;
	}
}

double log(double x)
{
	double hi, lo;

	if (x == 1.0)
		return 0.0;
	if (x < 0.0)
		return (x - x) / (x - x);	/* log(negative) = nan */
	if (x == 0.0)
		return -1.0 / 0.0;		/* log(+-0) = -inf */
	if ((asu64(x) << 1) >= 0xffe0000000000000ull)
		return x;			/* +inf or nan */
	__brk_log_pair(x, &hi, &lo);
	return hi + lo;
}

double log10(double x)
{
	double hi, lo, p, err;

	if (x == 1.0)
		return 0.0;
	if (x <= 0.0 || (asu64(x) << 1) >= 0xffe0000000000000ull)
		return log(x);		/* same specials and nan image */
	__brk_log_pair(x, &hi, &lo);
	p = hi * LOG10E_HI;
	err = two_prod_err(hi, LOG10E_HI, p);
	return p + (err + hi * LOG10E_LO + lo * (LOG10E_HI + LOG10E_LO));
}

double log2(double x)
{
	double hi, lo, p, err;

	if (x == 1.0)
		return 0.0;
	if (x <= 0.0 || (asu64(x) << 1) >= 0xffe0000000000000ull)
		return log(x);		/* same specials and nan image */
	__brk_log_pair(x, &hi, &lo);
	p = hi * LOG2E_HI;
	err = two_prod_err(hi, LOG2E_HI, p);
	return p + (err + hi * LOG2E_LO + lo * (LOG2E_HI + LOG2E_LO));
}
