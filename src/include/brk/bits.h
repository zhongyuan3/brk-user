#ifndef _BRK_BITS_H
#define _BRK_BITS_H

#include <stdint.h>

/*
 * Small compiler-runtime replacements for userland.
 *
 * These helpers intentionally use loops/masks instead of __builtin_clz*,
 * __builtin_ctz* and __builtin_bswap* so that the compiler cannot lower them
 * back into a libgcc/compiler-rt helper call.  The volatile accesses prevent
 * the optimizer from recognizing the idiomatic patterns.
 */

static inline int brk_clz64(uint64_t value)
{
	int n = 0;
	volatile uint64_t v = value;

	if (!v)
		return 64;

	while (!(v & (UINT64_C(1) << 63))) {
		v <<= 1;
		n++;
	}
	return n;
}

static inline int brk_ctz64(uint64_t value)
{
	int n = 0;
	volatile uint64_t v = value;

	if (!v)
		return 64;

	while (!(v & 1)) {
		v >>= 1;
		n++;
	}
	return n;
}

static inline uint32_t brk_bswap32(uint32_t value)
{
	volatile uint32_t v = value;

	return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
	       ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

static inline uint16_t brk_bswap16(uint16_t value)
{
	volatile uint16_t v = value;

	return (uint16_t)(((v & 0x00ffu) << 8) | ((v & 0xff00u) >> 8));
}

#endif
