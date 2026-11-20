#include <stdio.h>
#include <unistd.h>

#include "stdio_impl.h"

long ftell(FILE *stream)
{
	off_t pos;

	if (!stream || stream->fd < 0)
		return -1;
	if (fflush(stream) < 0)
		return -1;
	pos = lseek(stream->fd, 0, SEEK_CUR);
	if (pos < 0)
		return -1;
	return (long)pos;
}
