#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	if (!stream || size == 0 || nmemb == 0)
		return 0;

	size_t left = size * nmemb;
	uint8_t *p = ptr;

	while (left > 0) {
		ssize_t r = read(stream->fd, p, left);
		if (r < 0)
			return 0;
		if (r == 0)
			break;
		left -= r;
		p += r;
	}

	return (p - (uint8_t *)ptr) / size;
}
