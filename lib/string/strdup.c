#include <stdlib.h>
#include <string.h>

char *strdup(char const *s)
{
	size_t len = strlen(s);
	char *p = malloc(len + 1);
	if (!p)
		return NULL;
	memcpy(p, s, len + 1);
	return p;
}
