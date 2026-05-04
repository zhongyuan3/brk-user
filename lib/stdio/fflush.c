#include <stdio.h>
#include <unistd.h>

#include "stdio_int.h"

int __stdio_flush_wbuf(FILE *stream)
{
	if (!stream || stream->fd < 0 || stream->buf_used == 0)
		return 0;
	if (!stream->buf)
		return 0;
	char *p = stream->buf;
	char *end = p + stream->buf_used;
	while (p < end) {
		ssize_t n = write(stream->fd, p, end - p);
		if (n < 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
		if (n == 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
		p += n;
	}
	stream->buf_used = 0;
	return 0;
}

int __stdio_purge_readbuf(FILE *stream)
{
	if (!stream || stream->fd < 0)
		return 0;
	if (stream->read_ptr && stream->read_end &&
	    stream->read_ptr < stream->read_end) {
		off_t back = (off_t)(stream->read_end - stream->read_ptr);
		if (lseek(stream->fd, -back, SEEK_CUR) < 0) {
			stream->flags |= __IO_ERR;
			return -1;
		}
	}
	stream->read_ptr = NULL;
	stream->read_end = NULL;
	return 0;
}

int fflush(FILE *stream)
{
	if (!stream) {
		int a = fflush(stdout);
		int b = fflush(stderr);
		return (a < 0 || b < 0) ? -1 : 0;
	}
	if (__stdio_flush_wbuf(stream) < 0)
		return -1;
	if (__stdio_purge_readbuf(stream) < 0)
		return -1;
	return 0;
}
