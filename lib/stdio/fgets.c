#include <stdio.h>

char *fgets(char *s, int size, FILE *stream)
{
	char *p;
	int c;

	if (!s || size <= 0 || !stream)
		return NULL;

	p = s;
	while (size > 1) {
		c = fgetc(stream);
		if (c == EOF) {
			if (ferror(stream))
				return NULL;
			if (p == s)
				return NULL;
			break;
		}
		*p++ = (char)c;
		size--;
		if (c == '\n')
			break;
	}
	*p = '\0';
	return s;
}
