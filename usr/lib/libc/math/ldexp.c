/*
 * Double-precision ldexp.
 *
 * Adapted from musl: Copyright (c) 2005-2020 Rich Felker and the musl
 * libc authors.  SPDX-License-Identifier: MIT.
 */

#include <math.h>

double ldexp(double x, int n)
{
	return scalbn(x, n);
}
