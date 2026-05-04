#include <string.h>

char *strrchr(const char *s, int c)
{
	unsigned char uc = (unsigned char)c;
	const char *last = NULL;

	do {
		if (*(unsigned char *)s == uc)
			last = s;
	} while (*s++ != '\0');

	return (char *)last;
}
