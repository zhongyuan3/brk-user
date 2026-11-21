#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

/*
 * Case-insensitive substring search (BSD interface).
 */
char *strcasestr(const char *haystack, const char *needle)
{
	char fc;
	size_t nlen;

	if (!haystack || !needle)
		return NULL;
	if (!*needle)
		return (char *)haystack;

	fc = (char)tolower((unsigned char)*needle);
	nlen = strlen(needle);
	for (; *haystack; haystack++) {
		if (tolower((unsigned char)*haystack) != fc)
			continue;
		if (!strncasecmp(haystack, needle, nlen))
			return (char *)haystack;
	}
	return NULL;
}
