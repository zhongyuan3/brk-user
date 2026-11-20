/*
 * Math error helpers used by the vendored exp/log/pow implementations.
 *
 * Adapted from musl: Copyright (c) 2005-2020 Rich Felker and the musl
 * libc authors.  SPDX-License-Identifier: MIT.
 * The original musl functions raise the IEEE overflow/underflow and
 * divide-by-zero exceptions through fp_barrier; the BRK kernel does not
 * expose FPU exception state to user space, so only the result values
 * matter here.
 */

#include "libm.h"

double __math_xflow(uint32_t sign, double y)
{
	return eval_as_double(fp_barrier(sign ? -y : y) * y);
}

double __math_uflow(uint32_t sign)
{
	return __math_xflow(sign, 0x1p-767);
}

double __math_oflow(uint32_t sign)
{
	return __math_xflow(sign, 0x1p769);
}

double __math_divzero(uint32_t sign)
{
	return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}

double __math_invalid(double x)
{
	return (x - x) / (x - x);
}
