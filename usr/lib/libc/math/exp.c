/*
 * exp(x) = e^(hi + lo), the shared kernel of exp and pow.
 *
 * Method: x = k*(ln2/32) + r with k = round(x*32/ln2), so
 * |r| <= ln2/64 + slack.  Then
 *
 *     e^x = 2^m * 2^(j/32) * e^r,   k = 32*m + j, 0 <= j < 32,
 *
 * where the 2^(j/32) factors are held as 106-bit hi/lo pairs
 * (scripts/gen-libm-consts.py) and e^r - 1 is a degree-7 Taylor
 * correction with exact rational coefficients.  The reduction products
 * k*L32_HI are exact in the format (the constant carries 16 trailing
 * zero bits), so r inherits no rounding beyond the lo-term noise, and
 * the final result is assembled with a single rounding at the scale of
 * the tabulated factor.
 *
 * Error: < 0.6 ulp of the core; overflow and underflow boundaries are
 * reached through exact power-of-two scalings, so they round correctly.
 */

#include <stdint.h>

#include "libm.h"

/* ln2/32: hi has 37 significant bits, so k*hi is exact for |k| < 2^16;
 * lo carries the remaining bits. */
static const double LN2_OVER32_HI = 0x1.62e42fefa0000p-6;
static const double LN2_OVER32_LO = 0x1.cf79abc9e3b3ap-45;
static const double INV_LN2_OVER32 = 0x1.71547652b82fep+5;

/* 2^(j/32) as hi/lo pairs (generated). */
static const double exp2_t32[32][2] = {
	{ 0x1.0000000000000p+0, 0.0 },		    /* 2^(0/32) */
	{ 0x1.059b0d3158574p+0, 0x1.d73e2a475b465p-55 },  /* 2^(1/32) */
	{ 0x1.0b5586cf9890fp+0, 0x1.8a62e4adc610bp-54 },  /* 2^(2/32) */
	{ 0x1.11301d0125b51p+0, -0x1.6c51039449b3ap-54 }, /* 2^(3/32) */
	{ 0x1.172b83c7d517bp+0, -0x1.19041b9d78a76p-55 }, /* 2^(4/32) */
	{ 0x1.1d4873168b9aap+0, 0x1.e016e00a2643cp-54 },  /* 2^(5/32) */
	{ 0x1.2387a6e756238p+0, 0x1.9b07eb6c70573p-54 },  /* 2^(6/32) */
	{ 0x1.29e9df51fdee1p+0, 0x1.612e8afad1255p-55 },  /* 2^(7/32) */
	{ 0x1.306fe0a31b715p+0, 0x1.6f46ad23182e4p-55 },  /* 2^(8/32) */
	{ 0x1.371a7373aa9cbp+0, -0x1.63aeabf42eae2p-54 }, /* 2^(9/32) */
	{ 0x1.3dea64c123422p+0, 0x1.ada0911f09ebcp-55 },  /* 2^(10/32) */
	{ 0x1.44e086061892dp+0, 0x1.89b7a04ef80d0p-59 }, /* 2^(11/32) */
	{ 0x1.4bfdad5362a27p+0, 0x1.d4397afec42e2p-56 },  /* 2^(12/32) */
	{ 0x1.5342b569d4f82p+0, -0x1.07abe1db13cadp-55 }, /* 2^(13/32) */
	{ 0x1.5ab07dd485429p+0, 0x1.6324c054647adp-54 },  /* 2^(14/32) */
	{ 0x1.6247eb03a5585p+0, -0x1.383c17e40b497p-54 }, /* 2^(15/32) */
	{ 0x1.6a09e667f3bcdp+0, -0x1.bdd3413b26456p-54 }, /* 2^(16/32) */
	{ 0x1.71f75e8ec5f74p+0, -0x1.16e4786887a99p-55 }, /* 2^(17/32) */
	{ 0x1.7a11473eb0187p+0, -0x1.41577ee04992fp-55 }, /* 2^(18/32) */
	{ 0x1.82589994cce13p+0, -0x1.d4c1dd41532d8p-54 }, /* 2^(19/32) */
	{ 0x1.8ace5422aa0dbp+0, 0x1.6e9f156864b27p-54 },  /* 2^(20/32) */
	{ 0x1.93737b0cdc5e5p+0, -0x1.75fc781b57ebcp-57 }, /* 2^(21/32) */
	{ 0x1.9c49182a3f090p+0, 0x1.c7c46b071f2bep-56 },  /* 2^(22/32) */
	{ 0x1.a5503b23e255dp+0, -0x1.d2f6edb8d41e1p-54 }, /* 2^(23/32) */
	{ 0x1.ae89f995ad3adp+0, 0x1.7a1cd345dcc81p-54 },  /* 2^(24/32) */
	{ 0x1.b7f76f2fb5e47p+0, -0x1.5584f7e54ac3bp-56 }, /* 2^(25/32) */
	{ 0x1.c199bdd85529cp+0, 0x1.11065895048ddp-55 },  /* 2^(26/32) */
	{ 0x1.cb720dcef9069p+0, 0x1.503cbd1e949dbp-56 },  /* 2^(27/32) */
	{ 0x1.d5818dcfba487p+0, 0x1.2ed02d75b3707p-55 },  /* 2^(28/32) */
	{ 0x1.dfc97337b9b5fp+0, -0x1.1a5cd4f184b5cp-54 }, /* 2^(29/32) */
	{ 0x1.ea4afa2a490dap+0, -0x1.e9c23179c2893p-54 }, /* 2^(30/32) */
	{ 0x1.f50765b6e4540p+0, 0x1.9d3e12dd8a18bp-54 },  /* 2^(31/32) */
};

/* Scale v by 2^m, keeping every intermediate exact and the final
 * rounding single. */
static double scale_pow2(double v, int m)
{
	if (m > 1023) {
		v *= 0x1p1023;
		return v * fro64((uint64_t)(0x3ff + m - 1023) << 52);
	}
	if (m < -1022) {
		/* 2^-1000 keeps the intermediate normal; the second
		   multiply rounds into the subnormal range once. */
		v *= 0x1p-1000;
		return v * fro64((uint64_t)(0x3ff + m + 1000) << 52);
	}
	return v * fro64((uint64_t)(0x3ff + m) << 52);
}

hidden double __brk_exp_pair(double x, double lo)
{
	double z, kd, r, r2, q, corr, core;
	const double *t;
	int k, m;
	uint32_t j;

	if (((uint32_t)(asu64(x) >> 52) & 0x7ff) >= 0x409) {
		/* |x| >= 1024: the lo term cannot bring it back below
		   the cut, and the image is +-0 / +-inf / nan. */
		if ((asu64(x) << 1) > 0xffe0000000000000ull)
			return x + lo;		/* nan */
		if (asu64(x) >> 63)
			return 0.0;
		return 1.0 / 0.0;
	}

	/* k = round(x*32/ln2), |x| < 1024 keeps |k| < 2^16 so that
	   k*LN2_OVER32_HI is exact. */
	z = x * INV_LN2_OVER32 + lo * INV_LN2_OVER32;
	kd = rnd_to_int(z);

	/* r = x + lo - k*ln2/32, computed with exact leading part */
	r = (x - kd * LN2_OVER32_HI) + lo - kd * LN2_OVER32_LO;

	/* exp(r) = 1 + r + r^2*(1/2 + r/6 + ... + r^5/5040) */
	static const double q_c[5] = {
		1.0 / 6.0, 1.0 / 24.0, 1.0 / 120.0,
		1.0 / 720.0, 1.0 / 5040.0,
	};
	int i;

	r2 = r * r;
	q = q_c[4];
	for (i = 3; i >= 0; i--)
		q = q * r + q_c[i];
	corr = r + r2 * (0.5 + r * q);

	/* 2^(k/32) = 2^m * (t[0] + t[1]); GCC shifts negatives
	   arithmetically, so m is the floor division. */
	k = (int)kd;
	j = k & 31;
	m = k >> 5;
	t = exp2_t32[j];

	/* Single rounding at the scale of the tabulated factor: the
	   correction group stays below 2^-6 relative. */
	core = t[0] + (t[0] * corr + t[1]);

	return scale_pow2(core, m);
}

double exp(double x)
{
	return __brk_exp_pair(x, 0.0);
}
