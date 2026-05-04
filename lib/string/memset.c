#include <string.h>

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	unsigned char uc = (unsigned char)c;

	for (size_t i = 0; i < n; ++i)
		p[i] = uc;

	return s;
}
