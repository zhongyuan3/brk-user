#include <brk/macros.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "stdio_int.h"

static int file_write_sync(FILE *stream, const char *buf, size_t len,
			   size_t *wlen)
{
	const size_t orig = len;

	while (len > 0) {
		size_t chunk = min(len, stream->buf_size - stream->buf_used);
		memcpy(stream->buf + stream->buf_used, buf, chunk);
		stream->buf_used += chunk;
		len -= chunk;
		buf += chunk;
		if (stream->buf_used >= stream->buf_size && fflush(stream) < 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
	}
	if (fflush(stream) < 0) {
		stream->flags |= __IO_ERR;
		return -1;
	}
	if (wlen)
		*wlen = orig;
	return 0;
}

static int file_write(FILE *stream, const char *buf, size_t len, size_t *wlen)
{
	const size_t orig = len;

	while (len > 0) {
		if (stream->buf_used >= stream->buf_size && fflush(stream) < 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
		int c = *buf++;
		stream->buf[stream->buf_used++] = (char)c;
		if (c == '\n' && fflush(stream) < 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
		--len;
	}
	if (wlen)
		*wlen = orig;
	return 0;
}

int __stdio_file_write(FILE *stream, char const *buf, size_t len, size_t *wlen)
{
	if (!stream || stream->flags & __IO_ERR)
		return -1;
	if (__stdio_purge_readbuf(stream) < 0)
		return -1;
	if (len == 0) {
		if (wlen)
			*wlen = 0;
		return 0;
	}
	if (stream->flags & __IO_SYNC)
		return file_write_sync(stream, buf, len, wlen);
	return file_write(stream, buf, len, wlen);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t total;
	size_t w = 0;

	if (!stream || size == 0 || nmemb == 0)
		return 0;

	total = size * nmemb;
	if (__stdio_file_write(stream, ptr, total, &w) < 0)
		return 0;
	return w / size;
}
