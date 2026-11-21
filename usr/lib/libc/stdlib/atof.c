#include <stdlib.h>

/*
 * atof(s) is exactly strtod(s, NULL) per C99; the end pointer is
 * simply discarded.
 */
double atof(const char *nptr)
{
	return strtod(nptr, NULL);
}
