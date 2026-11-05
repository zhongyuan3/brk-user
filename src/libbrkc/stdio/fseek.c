#include <stdio.h>
#include <unistd.h>

#include "stdio_impl.h"

int fseek(FILE *stream, long offset, int whence)
{
	if (!stream || stream->fd < 0)
		return -1;
	if (fflush(stream) < 0)
		return -1;
	off_t r = lseek(stream->fd, offset, whence);
	if (r < 0)
		return -1;
	stream->flags &= (unsigned char) ~__IO_EOF;
	return 0;
}
