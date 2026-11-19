#include <string.h>

char *strpbrk(const char *s, const char *accept)
{
	for (; *s; ++s) {
		for (const char *a = accept; *a; ++a) {
			if (*s == *a)
				return (char *)s;
		}
	}
	return NULL;
}
