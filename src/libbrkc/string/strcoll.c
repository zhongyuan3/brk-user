#include <string.h>

/*
 * Locale-aware comparison.  The BRK libc supports only the "C" locale,
 * where collation is plain byte order, so strcoll is strcmp.
 */
int strcoll(const char *s1, const char *s2)
{
	return strcmp(s1, s2);
}
