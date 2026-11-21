/*
 * Sign, classification and copies helpers.
 *
 * Pure bit manipulation: exact in every rounding mode, no floating
 * point arithmetic involved.
 */

#include <math.h>
#include <stdint.h>

#include "libm.h"

int __fpclassify(double x)
{
	union brk_dbits u = { x };
	int e = (int)(u.i >> 52) & 0x7ff;

	if (e == 0x7ff)
		return (u.i << 12) ? FP_NAN : FP_INFINITE;
	if (e == 0)
		return (u.i << 12) ? FP_SUBNORMAL : FP_ZERO;
	return FP_NORMAL;
}

int __signbit(double x)
{
	return (int)(asu64(x) >> 63);
}

double copysign(double x, double y)
{
	union brk_dbits ux = { x }, uy = { y };

	ux.i = (ux.i & ~(1ULL << 63)) | (uy.i & (1ULL << 63));
	return ux.f;
}

double fabs(double x)
{
	return fro64(asu64(x) & ~(1ULL << 63));
}
