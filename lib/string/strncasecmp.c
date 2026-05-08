#include <ctype.h>
#include <strings.h>

int strncasecmp(const char *s1, const char *s2, size_t n)
{
	if (n == 0)
		return 0;

	while (n > 0 && *s1 && *s2 && tolower(*s1) == tolower(*s2)) {
		++s1;
		++s2;
		--n;
	}

	return n == 0 ? 0 : (int)tolower(*s1) - (int)tolower(*s2);
}
