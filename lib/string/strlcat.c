#include <string.h>

size_t strlcat(char *dst, char const *src, size_t size)
{
	size_t len = strnlen(dst, size);
	if (len == size)
		return size + strlen(src);
	return len + strlcpy(dst + len, src, size - len);
}
