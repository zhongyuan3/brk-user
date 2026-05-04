#include <string.h>

char *strncpy(char *dst, const char *src, size_t n)
{
	char *ret = dst;
	size_t i;

	for (i = 0; i < n && src[i] != '\0'; ++i)
		dst[i] = src[i];

	for (; i < n; ++i)
		dst[i] = '\0';

	return ret;
}
