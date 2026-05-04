#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream)
{
	size_t len = 0;
	ssize_t r;
	char *buf = NULL;

	while (1) {
		if (len == 0) {
			if (buf == NULL) {
				buf = malloc(128);
				if (buf == NULL) {
					return -1;
				}
				buf[0] = '\0';
			}
			if (len < *n) {
				buf[len] = '\0';
				*lineptr = buf;
				return len;
			}
			buf = realloc(buf, len * 2);
			if (buf == NULL) {
				return -1;
			}
			*n = len * 2;
		}
		r = read(stream->fd, buf + len, 1);
		if (r < 0) {
			return -1;
		}
		if (r == 0) {
			break;
		}
		len++;
		if (buf[len - 1] == delimiter) {
			break;
		}
	}
	buf[len] = '\0';
	*lineptr = buf;
	return len;
}
