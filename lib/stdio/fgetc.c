#include <stdio.h>
#include <unistd.h>

#include "stdio_int.h"

int fgetc(FILE *stream)
{
	unsigned char c;

	if (!stream || (stream->flags & __IO_ERR))
		return EOF;

	if (__stdio_flush_wbuf(stream) < 0)
		return EOF;

	if (stream->read_ptr && stream->read_end &&
	    stream->read_ptr < stream->read_end) {
		c = (unsigned char)*stream->read_ptr++;
		return (int)c;
	}

	if (stream->flags & __IO_EOF)
		return EOF;

	if (!stream->buf || stream->buf_size == 0) {
		ssize_t r = read(stream->fd, &c, 1);
		if (r < 0) {
			stream->flags |= __IO_ERR;
			return EOF;
		}
		if (r == 0) {
			stream->flags |= __IO_EOF;
			return EOF;
		}
		return (int)c;
	}

	ssize_t r = read(stream->fd, stream->buf, stream->buf_size);
	if (r < 0) {
		stream->flags |= __IO_ERR;
		return EOF;
	}
	if (r == 0) {
		stream->flags |= __IO_EOF;
		return EOF;
	}
	stream->read_ptr = stream->buf;
	stream->read_end = stream->buf + r;
	c = (unsigned char)*stream->read_ptr++;
	return (int)c;
}

int getc(FILE *stream)
{
	return fgetc(stream);
}

int getchar(void)
{
	return fgetc(stdin);
}
