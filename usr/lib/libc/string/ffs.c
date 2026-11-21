#include <stdint.h>

#include "../internal/bits.h"

/*
 * Find-first-set bit, LSB first.  Zero is handled the C99 way: ffs(0)
 * returns 0, otherwise the bit position is 1-based.  The helpers use
 * brk_ctz64 so the compiler cannot emit a libgcc call.
 */
int ffs(int i)
{
	if (!i)
		return 0;
	return brk_ctz64((uint32_t)i) + 1;
}

int ffsl(long i)
{
	if (!i)
		return 0;
	return brk_ctz64((unsigned long)i) + 1;
}

int ffsll(long long i)
{
	if (!i)
		return 0;
	return brk_ctz64((unsigned long long)i) + 1;
}
