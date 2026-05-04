#include <stdio.h>
#include <unistd.h>

int fflush(FILE *stream)
{
	if (stream->buf_used == 0)
		return 0;
	char *p = stream->buf;
	char *end = p + stream->buf_used;
	while (p < end) {
		ssize_t n = write(stream->fd, p, end - p);
		if (n < 0)
			return -1;
		p += n;
	}
	stream->buf_used = 0;
	return 0;
}
