#include <stdio.h>
#include <string.h>

int fputs(const char *s, FILE *stream)
{
	size_t n = strlen(s);
	return fwrite(s, 1, n, stream) == n ? 0 : EOF;
}
