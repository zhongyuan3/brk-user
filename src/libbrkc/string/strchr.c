#include <string.h>

char *strchr(const char *s, int c)
{
	unsigned char uc = (unsigned char)c;
	while (*s != '\0') {
		if (*(unsigned char *)s == uc)
			return (char *)s;
		++s;
	}

	if (uc == '\0')
		return (char *)s;

	return NULL;
}
