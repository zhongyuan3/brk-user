/*
 * Bit-level floating-point helpers backing the public classification
 * macros in <math.h>.
 */

#include <math.h>
#include <stdint.h>

int __fpclassify(double x)
{
	union {
		double d;
		uint64_t i;
	} u = { x };
	int e = (int)(u.i >> 52) & 0x7ff;

	if (e == 0x7ff)
		return (u.i << 12) ? FP_NAN : FP_INFINITE;
	if (e == 0)
		return (u.i << 12) ? FP_SUBNORMAL : FP_ZERO;
	return FP_NORMAL;
}

int __signbit(double x)
{
	union {
		double d;
		uint64_t i;
	} u = { x };

	return (int)(u.i >> 63);
}

double copysign(double x, double y)
{
	union {
		double d;
		uint64_t i;
	} ux = { x }, uy = { y };

	ux.i &= ~((uint64_t)1 << 63);
	ux.i |= uy.i & ((uint64_t)1 << 63);
	return ux.d;
}
