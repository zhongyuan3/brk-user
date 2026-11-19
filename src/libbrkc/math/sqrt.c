/*
 * Square root via the RISC-V fsqrt.d instruction (F/D extensions).
 * The hardware handles negative and NaN inputs per IEEE 754: sqrt(-1)
 * returns NaN and sqrt(NaN) returns NaN, so no special casing is
 * needed for the semantics Lua and libm callers expect.
 */

#include <math.h>

double sqrt(double x)
{
	double r;

#if defined(__riscv)
	asm("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
	return r;
#else
	/* Host validation builds: the builtin lowers to the native
	 * square-root instruction with the same IEEE semantics. */
	r = __builtin_sqrt(x);
	return r;
#endif
}
