#include <string.h>

/*
 * BSD/GNU field splitter: writes '\0' over the next separator and
 * returns the token; *saveptr always addresses the next field.
 */
char *strsep(char **stringp, const char *delim)
{
	char *tok;
	char *p;

	if (!stringp || !*stringp)
		return NULL;
	if (!delim || !*delim) {
		tok = *stringp;
		*stringp = NULL;
		return tok;
	}

	tok = *stringp;
	p = tok;
	while (*p && !strchr(delim, *p))
		p++;
	if (*p)
		*p++ = '\0';
	*stringp = *p ? p : NULL;
	return tok;
}
