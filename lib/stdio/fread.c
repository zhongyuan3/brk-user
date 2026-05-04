#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "stdio_int.h"

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	if (!stream || size == 0 || nmemb == 0)
		return 0;
	if (stream->flags & __IO_ERR)
		return 0;

	size_t left = size * nmemb;
	uint8_t *p = ptr;

	if (__stdio_flush_wbuf(stream) < 0)
		return 0;

	while (left > 0) {
		while (left > 0 && stream->read_ptr && stream->read_end &&
		       stream->read_ptr < stream->read_end) {
			size_t chunk = (size_t)(stream->read_end - stream->read_ptr);
			if (chunk > left)
				chunk = left;
			memcpy(p, stream->read_ptr, chunk);
			stream->read_ptr += chunk;
			p += chunk;
			left -= chunk;
		}

		if (left == 0)
			break;

		if (!stream->buf || stream->buf_size == 0) {
			ssize_t r = read(stream->fd, p, left);
			if (r < 0) {
				stream->flags |= __IO_ERR;
				break;
			}
			if (r == 0) {
				stream->flags |= __IO_EOF;
				break;
			}
			p += r;
			left -= (size_t)r;
			continue;
		}

		ssize_t r = read(stream->fd, stream->buf, stream->buf_size);
		if (r < 0) {
			stream->flags |= __IO_ERR;
			break;
		}
		if (r == 0) {
			stream->flags |= __IO_EOF;
			break;
		}
		stream->read_ptr = stream->buf;
		stream->read_end = stream->buf + r;
	}

	return (p - (uint8_t *)ptr) / size;
}
