#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream)
{
	char *buf;
	size_t cap;
	size_t len = 0;

	if (!lineptr || !n || !stream) {
		errno = EINVAL;
		return -1;
	}

	if (*lineptr == NULL) {
		buf = NULL;
		cap = 0;
	} else {
		buf = *lineptr;
		cap = *n;
	}

	while (1) {
		if (len + 2 > cap) {
			size_t ncap = cap ? cap * 2 : 128;
			if (ncap < len + 2)
				ncap = len + 2;
			char *nb = realloc(buf, ncap);
			if (!nb) {
				errno = ENOMEM;
				return -1;
			}
			buf = nb;
			cap = ncap;
		}

		{
			unsigned char c;
			size_t nr = fread(&c, 1, 1, stream);
			if (nr == 0) {
				if (ferror(stream))
					return -1;
				if (len == 0) {
					errno = 0;
					return -1;
				}
				break;
			}
			buf[len++] = (char)c;
		}
		if (buf[len - 1] == (char)delimiter)
			break;
	}

	buf[len] = '\0';
	*lineptr = buf;
	*n = cap;
	return (ssize_t)len;
}
