#include <string.h>

size_t strlcpy(char *dst, char const *src, size_t size)
{
	const char *const src0 = src;

	if (size == 0)
		return strlen(src);

	size_t left = size - 1;
	while (left && *src) {
		*dst++ = *src++;
		left--;
	}
	*dst = '\0';
	return (size_t)(src - src0) + strlen(src);
}
