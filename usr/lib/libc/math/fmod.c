/*
 * fmod: exact floating remainder.
 *
 * The significands are reduced with an integer shift-subtract loop,
 * which is the only way to keep the result exact.  fmod(x, y) is always
 * exactly representable (the remainder lies on the representable grid
 * shared by x and y), so no rounding ever occurs.
 */

#include <stdint.h>

#include "libm.h"

/* Normalize a nonzero finite double to (m, e) with m having its leading
 * bit at position 52 and value == m * 2^(e - 1075).  For normals e is
 * the raw exponent field; for subnormals it can reach below 1. */
static void norm_sig(uint64_t bits, uint64_t *m, int *e)
{
	uint64_t frac = bits & 0xfffffffffffffull;
	int raw = (int)(bits >> 52) & 0x7ff;

	if (raw == 0) {
		int lz = __builtin_clzll(frac) - 11;

		*m = frac << lz;
		*e = 1 - lz;
	} else {
		*m = frac | (1ULL << 52);
		*e = raw;
	}
}

double fmod(double x, double y)
{
	union brk_dbits ux = { x }, uy = { y };
	uint64_t mx, my, m;
	int ex, ey, d;

	if (uy.i << 1 == 0 ||
	    (ux.i << 1) >= 0xffe0000000000000ull ||
	    (uy.i << 1) > 0xffe0000000000000ull)
		return (x * y) / (x * y);   /* y=0, x inf/nan, y nan */
	if ((ux.i << 1) == (uy.i << 1))
		return x * 0.0;          /* |x| == |y|: signed zero */
	if ((ux.i << 1) < (uy.i << 1))
		return x;                /* |x| < |y| */

	norm_sig(ux.i, &mx, &ex);
	norm_sig(uy.i, &my, &ey);

	/* Long division of mx * 2^(ex-ey) by my: at every step m < my, so
	   the shift stays inside 64 bits. */
	m = mx;
	for (d = ex - ey; d > 0; d--) {
		if (m >= my)
			m -= my;
		m <<= 1;
	}
	if (m >= my)
		m -= my;

	if (m == 0)
		return x * 0.0;
	{
		int lz = __builtin_clzll(m) - 11;
		int eout = ey - lz;

		m <<= lz;
		if (eout < 1) {
			/* subnormal result: exact by the grid argument */
			m >>= 1 - eout;
			ux.i = (ux.i & (1ULL << 63)) | m;
		} else {
			ux.i = (ux.i & (1ULL << 63)) |
			       ((uint64_t)eout << 52) |
			       (m & 0xfffffffffffffull);
		}
	}
	return ux.f;
}
