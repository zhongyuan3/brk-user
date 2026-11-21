#include <string.h>

/*
 * Reentrant strtok(): the caller keeps the position in *saveptr.
 */
char *strtok_r(char *str, const char *delim, char **saveptr)
{
	char *p;
	char *tok;

	if (!delim || !saveptr)
		return NULL;
	p = str ? str : *saveptr;
	if (!p)
		return NULL;

	while (*p && strchr(delim, *p))
		p++;
	if (!*p) {
		*saveptr = p;
		return NULL;
	}
	tok = p;
	while (*p && !strchr(delim, *p))
		p++;
	if (*p) {
		*p = '\0';
		*saveptr = p + 1;
	} else {
		*saveptr = p;
	}
	return tok;
}
