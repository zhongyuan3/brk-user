#include <string.h>

void memswap(void *s1, void *s2, size_t size)
{
	unsigned char t;
	unsigned char *p1 = s1;
	unsigned char *p2 = s2;

	for (size_t i = 0; i < size; ++i) {
		t = p1[i];
		p1[i] = p2[i];
		p2[i] = t;
	}
}
