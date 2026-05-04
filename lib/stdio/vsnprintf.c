#include <brk/macros.h>
#include <stdio.h>
#include <string.h>

#include "stdio_impl.h"

static int snprintf_write(FILE *stream, char const *buf, size_t len,
			  size_t *written)
{
	size_t total = 0;

	if (stream->buf_size == 0) {
		if (written)
			*written = len;
		return 0;
	}

	while (len > 0) {
		size_t room = stream->buf_size - stream->buf_used;
		if (room == 0) {
			if (written)
				*written = total + len;
			return 0;
		}
		size_t chunk = min(len, room);
		memcpy(stream->buf + stream->buf_used, buf, chunk);
		stream->buf_used += chunk;
		buf += chunk;
		len -= chunk;
		total += chunk;
	}
	if (written)
		*written = total;
	return 0;
}

int vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
	FILE stream = {
		.write = snprintf_write,
		.buf = buf,
		.buf_used = 0,
		.buf_size = size,
		.read_ptr = NULL,
		.read_end = NULL,
		.fd = -1,
		.flags = 0,
	};
	int n;

	if (size > 0 && !buf)
		return -1;
	n = vfprintf(&stream, format, ap);
	if (n < 0)
		return n;
	if (size > 0) {
		if ((size_t)n >= size) {
			buf[size - 1] = '\0';
		} else {
			buf[n] = '\0';
		}
	}
	return n;
}
