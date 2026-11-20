/*
 * Correctly rounded floating-point conversions for printf: %e %f %g %a
 * and their uppercase forms.
 *
 * The engine is vendored from musl (src/stdio/vfprintf.c):
 *   Copyright (c) 2005-2020 Rich Felker and the musl libc authors
 *   SPDX-License-Identifier: MIT
 * Changes for the BRK libc:
 *   - double precision only: the long-double paths are dropped and
 *     every constant is the double one, which also shrinks the working
 *     array to a fixed size.
 *   - Output goes through the shared __printf_out/__printf_pad
 *     helpers instead of musl's FILE internals.
 *   - Flag bits are the shared PRTF_* definitions.
 * The big-decimal scaling and the round-by-probing scheme are kept
 * verbatim; they give correctly rounded output including halfway
 * cases, huge exponents and subnormals.
 */

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "stdio_int.h"

/*
 * The vendored musl engine keeps its own idioms (`*d & (1 << sh) - 1`,
 * unsigned/int mixes in the scaling loops) and branch pairs whose
 * initialization only the -O2 optimizer can prove (`estr` is set by
 * the non-'f' branch at the l computation and only used by the same
 * branch at output time, with t unchanged in between; -Og's weaker
 * dataflow flags it anyway).  Silence the warnings rather than
 * rewriting the algorithm.
 */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char xdigits[16] = {
	"0123456789ABCDEF"
};

static char *fmt_u(uintmax_t x, char *s)
{
	unsigned long y;

	for ( ; x > ULONG_MAX; x /= 10)
		*--s = '0' + x % 10;
	for (y = x; y >= 10; y /= 10)
		*--s = '0' + y % 10;
	if (y)
		*--s = '0' + y;
	return s;
}

/* musl pad(): emit (w - l) copies of c unless padding is suppressed by
 * the flags or the field is already full. */
static void fpad(struct fmt_context *ctx, char c, size_t w, size_t l,
		 unsigned fl)
{
	if (fl & (PRTF_LEFT_ALIGN | PRTF_ZERO_PAD) || l >= w)
		return;
	__printf_pad(ctx, w - l, c);
}

int __printf_fmt_fp(struct fmt_context *ctx, double y, size_t width, int p,
		    unsigned fl, int t)
{
	/* One slot for 29 bits left of radix point, a slot for every
	 * 29-21=8 bits right of the radix point, and one final zero slot. */
	enum { MAX_MANT_SLOTS = 1 + (DBL_MANT_DIG - 29 + 7) / 8 + 1 };
	enum { MAX_EXP_SLOTS = (DBL_MAX_EXP + DBL_MANT_DIG + 28 + 8) / 9 };
	enum { BIGSIZE = MAX_MANT_SLOTS + MAX_EXP_SLOTS + 4 };
	uint32_t big[BIGSIZE];
	uint32_t *a, *d, *r, *z;
	int e2 = 0, e, i, j, l;
	char buf[9 + DBL_MANT_DIG / 4], *s;
	const char *prefix = "-0X+0X 0X-0x+0x 0x";
	int pl;
	char ebuf0[3 * sizeof(int)], *ebuf = &ebuf0[3 * sizeof(int)], *estr;
	size_t w = width;

	pl = 1;
	if (signbit(y)) {
		y = -y;
	} else if (fl & PRTF_MARK_POS) {
		prefix += 3;
	} else if (fl & PRTF_PAD_POS) {
		prefix += 6;
	} else {
		prefix++, pl = 0;
	}

	if (!isfinite(y)) {
		const char *inf_nan = (t & 32) ? "inf" : "INF";

		if (y != y)
			inf_nan = (t & 32) ? "nan" : "NAN";
		fpad(ctx, ' ', w, 3 + (size_t)pl, fl & ~PRTF_ZERO_PAD);
		__printf_out(ctx, prefix, (size_t)pl);
		__printf_out(ctx, inf_nan, 3);
		fpad(ctx, ' ', w, 3 + (size_t)pl, fl ^ PRTF_LEFT_ALIGN);
		return (int)MAX(w, 3 + (size_t)pl);
	}

	y = frexp(y, &e2) * 2;
	if (y)
		e2--;

	if ((t | 32) == 'a') {
		if (t & 32)
			prefix += 9;
		pl += 2;

		if (p >= 0 && p < (DBL_MANT_DIG - 1 + 3) / 4) {
			double round = scalbn(1, DBL_MANT_DIG - 1 - (p * 4));

			if (*prefix == '-') {
				y = -y;
				y -= round;
				y += round;
				y = -y;
			} else {
				y += round;
				y -= round;
			}
		}

		estr = fmt_u(e2 < 0 ? -e2 : e2, ebuf);
		if (estr == ebuf)
			*--estr = '0';
		*--estr = (e2 < 0 ? '-' : '+');
		*--estr = t + ('p' - 'a');

		s = buf;
		do {
			int x = y;

			*s++ = xdigits[x] | (t & 32);
			y = 16 * (y - x);
			if (s - buf == 1 && (y || p > 0 || (fl & PRTF_ALT_FORM)))
				*s++ = '.';
		} while (y);

		if (p > INT_MAX - 2 - (ebuf - estr) - pl)
			return -1;
		if (p && s - buf - 2 < p)
			l = (p + 2) + (int)(ebuf - estr);
		else
			l = (int)(s - buf) + (int)(ebuf - estr);

		fpad(ctx, ' ', w, (size_t)(pl + l), fl);
		__printf_out(ctx, prefix, (size_t)pl);
		fpad(ctx, '0', w, (size_t)(pl + l), fl ^ PRTF_ZERO_PAD);
		__printf_out(ctx, buf, (size_t)(s - buf));
		fpad(ctx, '0', (size_t)(l - (ebuf - estr) - (s - buf)), 0, 0);
		__printf_out(ctx, estr, (size_t)(ebuf - estr));
		fpad(ctx, ' ', w, (size_t)(pl + l), fl ^ PRTF_LEFT_ALIGN);
		return (int)MAX(w, (size_t)(pl + l));
	}
	if (p < 0)
		p = 6;

	if (y)
		y *= 0x1p28, e2 -= 28;

	if (e2 < 0) {
		a = r = z = big;
	} else {
		a = r = z = big + sizeof(big) / sizeof(*big) - MAX_MANT_SLOTS - 1;
	}

	do {
		*z = y;
		y = 1000000000 * (y - *z++);
	} while (y);

	while (e2 > 0) {
		uint32_t carry = 0;
		int sh = MIN(29, e2);

		for (d = z - 1; d >= a; d--) {
			uint64_t x = ((uint64_t)*d << sh) + carry;

			*d = x % 1000000000;
			carry = x / 1000000000;
		}
		if (carry)
			*--a = carry;
		while (z > a && !z[-1])
			z--;
		e2 -= sh;
	}
	while (e2 < 0) {
		uint32_t carry = 0, *b;
		int sh = MIN(9, -e2);
		int need = 1 + (p + DBL_MANT_DIG / 3U + 8) / 9;

		for (d = a; d < z; d++) {
			uint32_t rm = *d & (1 << sh) - 1;

			*d = (*d >> sh) + carry;
			carry = (1000000000 >> sh) * rm;
		}
		if (!*a)
			a++;
		if (carry)
			*z++ = carry;
		/* Avoid (slow!) computation past requested precision */
		b = (t | 32) == 'f' ? r : a;
		if (z - b > need)
			z = b + need;
		e2 += sh;
	}

	if (a < z) {
		for (i = 10, e = 9 * (r - a); *a >= i; i *= 10, e++)
			;
	} else {
		e = 0;
	}

	/* Perform rounding: j is precision after the radix (possibly neg) */
	j = p - ((t | 32) != 'f') * e - ((t | 32) == 'g' && p);
	if (j < 9 * (z - r - 1)) {
		uint32_t x;

		/* We avoid C's broken division of negative numbers */
		d = r + 1 + ((j + 9 * DBL_MAX_EXP) / 9 - DBL_MAX_EXP);
		j += 9 * DBL_MAX_EXP;
		j %= 9;
		for (i = 10, j++; j < 9; i *= 10, j++)
			;
		x = *d % i;
		/* Are there any significant digits past j? */
		if (x || d + 1 != z) {
			double round = 2 / DBL_EPSILON;
			double small;

			if ((*d / i & 1) ||
			    (i == 1000000000 && d > a && (d[-1] & 1)))
				round += 2;
			if (x < i / 2) {
				small = 0x0.8p0;
			} else if (x == i / 2 && d + 1 == z) {
				small = 0x1.0p0;
			} else {
				small = 0x1.8p0;
			}
			if (pl && *prefix == '-')
				round *= -1, small *= -1;
			*d -= x;
			/* Decide whether to round by probing round+small */
			if (round + small != round) {
				*d = *d + i;
				while (*d > 999999999) {
					*d-- = 0;
					if (d < a)
						*--a = 0;
					(*d)++;
				}
				for (i = 10, e = 9 * (r - a); *a >= i; i *= 10, e++)
					;
			}
		}
		if (z > d + 1)
			z = d + 1;
	}
	for (; z > a && !z[-1]; z--)
		;

	if ((t | 32) == 'g') {
		if (!p)
			p++;
		if (p > e && e >= -4) {
			t--;
			p -= e + 1;
		} else {
			t -= 2;
			p--;
		}
		if (!(fl & PRTF_ALT_FORM)) {
			/* Count trailing zeros in last place */
			if (z > a && z[-1]) {
				for (i = 10, j = 0; z[-1] % i == 0; i *= 10, j++)
					;
			} else {
				j = 9;
			}
			if ((t | 32) == 'f')
				p = MIN(p, MAX(0, 9 * (z - r - 1) - j));
			else
				p = MIN(p, MAX(0, 9 * (z - r - 1) + e - j));
		}
	}
	if (p > INT_MAX - 1 - (p || (fl & PRTF_ALT_FORM)))
		return -1;
	l = 1 + p + (p || (fl & PRTF_ALT_FORM));
	if ((t | 32) == 'f') {
		if (e > INT_MAX - l)
			return -1;
		if (e > 0)
			l += e;
	} else {
		estr = fmt_u(e < 0 ? -e : e, ebuf);
		while (ebuf - estr < 2)
			*--estr = '0';
		*--estr = (e < 0 ? '-' : '+');
		*--estr = t;
		if (ebuf - estr > INT_MAX - l)
			return -1;
		l += (int)(ebuf - estr);
	}

	if (l > INT_MAX - pl)
		return -1;
	fpad(ctx, ' ', w, (size_t)(pl + l), fl);
	__printf_out(ctx, prefix, (size_t)pl);
	fpad(ctx, '0', w, (size_t)(pl + l), fl ^ PRTF_ZERO_PAD);

	if ((t | 32) == 'f') {
		if (a > r)
			a = r;
		for (d = a; d <= r; d++) {
			char *s = fmt_u(*d, buf + 9);

			if (d != a) {
				while (s > buf)
					*--s = '0';
			} else if (s == buf + 9) {
				*--s = '0';
			}
			__printf_out(ctx, s, (size_t)(buf + 9 - s));
		}
		if (p || (fl & PRTF_ALT_FORM))
			__printf_out(ctx, ".", 1);
		for (; d < z && p > 0; d++, p -= 9) {
			char *s = fmt_u(*d, buf + 9);

			while (s > buf)
				*--s = '0';
			__printf_out(ctx, s, (size_t)MIN(9, p));
		}
		fpad(ctx, '0', (size_t)(p + 9), 9, 0);
	} else {
		if (z <= a)
			z = a + 1;
		for (d = a; d < z && p >= 0; d++) {
			char *s = fmt_u(*d, buf + 9);

			if (s == buf + 9)
				*--s = '0';
			if (d != a) {
				while (s > buf)
					*--s = '0';
			} else {
				__printf_out(ctx, s++, 1);
				if (p > 0 || (fl & PRTF_ALT_FORM))
					__printf_out(ctx, ".", 1);
			}
			__printf_out(ctx, s, (size_t)MIN(buf + 9 - s, p));
			p -= (int)(buf + 9 - s);
		}
		fpad(ctx, '0', (size_t)(p + 18), 18, 0);
		__printf_out(ctx, estr, (size_t)(ebuf - estr));
	}

	fpad(ctx, ' ', w, (size_t)(pl + l), fl ^ PRTF_LEFT_ALIGN);

	return (int)MAX(w, (size_t)(pl + l));
}
