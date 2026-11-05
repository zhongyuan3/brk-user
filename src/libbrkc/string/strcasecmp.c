#include <ctype.h>
#include <strings.h>

int strcasecmp(const char *s1, const char *s2)
{
	while (*s1 && *s2 && tolower(*s1) == tolower(*s2)) {
		++s1;
		++s2;
	}
	return (int)tolower(*s1) - (int)tolower(*s2);
}
