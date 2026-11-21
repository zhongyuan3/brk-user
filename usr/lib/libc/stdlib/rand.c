#include <stdint.h>

/*
 * Deterministic PRNG with the C99 rand()/srand() contract: a fresh
 * program behaves as if srand(1) had been called, and the sequence
 * depends on nothing but the seed.  xorshift64* is the mixer; the
 * golden-ratio offset spreads close seeds apart and the zero guard
 * keeps the mixer state nonzero.
 */

#define RAND_GOLDEN UINT64_C(0x9e3779b97f4a7c15)
#define RAND_MULT UINT64_C(2685821657736338717)

/* 1 + RAND_GOLDEN: the state srand(1) builds. */
static uint64_t rand_state = UINT64_C(0x9e3779b97f4a7c16);

void srand(unsigned int seed)
{
	uint64_t s = (uint64_t)seed + RAND_GOLDEN;

	rand_state = s ? s : 1;
}

int rand(void)
{
	uint64_t s = rand_state;

	s ^= s >> 12;
	s ^= s << 25;
	s ^= s >> 27;
	rand_state = s;
	return (int)((s * RAND_MULT) >> 33);	/* 31-bit result */
}

int rand_r(unsigned int *seedp)
{
	uint64_t s = *seedp;

	if (!s)
		s = 1;
	s ^= s >> 12;
	s ^= s << 25;
	s ^= s >> 27;
	*seedp = (unsigned int)s;
	return (int)((s * RAND_MULT) >> 33);
}
