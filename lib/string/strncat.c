#include <string.h>

char *strncat(char *dst, char const *src, size_t n)
{
	char *d = dst;
	dst += strlen(dst);
	while (n && *src) {
		--n;
		*dst++ = *src++;
	}
	*dst++ = '\0';
	return d;
}
