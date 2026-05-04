#include <string.h>

void *memrchr(const void *s, int c, size_t n)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned char uc = (unsigned char)c;

	for (size_t i = n; i > 0; --i)
		if (p[i - 1] == uc)
			return (void *)(p + i - 1);

	return NULL;
}
