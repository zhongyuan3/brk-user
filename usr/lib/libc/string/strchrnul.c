#include <string.h>

/*
 * GNU extension: like strchr() but returns the NUL terminator instead
 * of NULL when the character is not found before it.
 */
char *strchrnul(const char *s, int c)
{
	char ch = (char)c;

	if (!s)
		return NULL;
	for (; *s != ch; s++) {
		if (!*s)
			return (char *)s;
	}
	return (char *)s;
}
