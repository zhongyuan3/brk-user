#include <string.h>

size_t strnlen(const char *s, size_t n)
{
	const char *p = s;
	while (n > 0 && *p != '\0') {
		++p;
		--n;
	}
	return (size_t)(p - s);
}
