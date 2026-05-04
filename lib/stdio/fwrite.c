#include <brk/macros.h>
#include <stdio.h>
#include <string.h>

static int file_write_sync(FILE *stream, const char *buf, size_t len,
			   size_t *wlen)
{
	while (len > 0) {
		size_t chunk = min(len, stream->buf_size - stream->buf_used);
		memcpy(stream->buf + stream->buf_used, buf, chunk);
		stream->buf_used += chunk;
		len -= chunk;
		buf += chunk;
		if (stream->buf_used >= stream->buf_size && fflush(stream) < 0)
			return -1;
	}
	if (fflush(stream) < 0)
		return -1;
	if (wlen)
		*wlen = len;
	return 0;
}

static int file_write(FILE *stream, const char *buf, const size_t len,
		      size_t *wlen)
{
	size_t left = len;
	while (left > 0) {
		if (stream->buf_used >= stream->buf_size && fflush(stream) < 0)
			return -1;
		int c = *buf++;
		stream->buf[stream->buf_used++] = c;
		if (c == '\n' && fflush(stream) < 0)
			return -1;
		--left;
	}
	if (wlen)
		*wlen = len;
	return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t written = 0;

	if (!stream || size == 0 || nmemb == 0)
		return 0;

	int ret = 0;

	if (!stream->sync)
		ret = file_write(stream, ptr, size * nmemb, &written);
	else
		ret = file_write_sync(stream, ptr, size * nmemb, &written);

	if (ret < 0)
		return 0;

	return written / size;
}
