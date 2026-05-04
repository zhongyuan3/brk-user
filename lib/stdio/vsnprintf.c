#include <brk/macros.h>
#include <stdio.h>
#include <string.h>

static int snprintf_write(FILE *stream, char const *buf, size_t len,
			  size_t *written)
{
	size_t chunk = min(len, stream->buf_size - stream->buf_used);
	if (chunk > 0) {
		memcpy(stream->buf + stream->buf_used, buf, chunk);
		stream->buf_used += chunk;
	}
	if (written)
		*written = chunk;
	return 0;
}

int vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
	FILE stream = {
		.write = snprintf_write,
		.buf = buf,
		.buf_used = 0,
		.buf_size = size,
		.fd = -1,
		.sync = false,
	};
	return vfprintf(&stream, format, ap);
}
