#include <string.h>

char *strstr(char const *haystack, char const *needle)
{
	if (*needle == '\0')
		return (char *)haystack;

	size_t needle_len = strlen(needle);
	size_t haystack_len = strlen(haystack);
	if (needle_len > haystack_len)
		return NULL;

	char const *p = haystack;
	char const *end = p + haystack_len - needle_len + 1;
	for (; p < end; ++p)
		if (memcmp(p, needle, needle_len) == 0)
			return (char *)p;

	return NULL;
}
