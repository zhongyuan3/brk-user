#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stdio_int.h"

/*
 * Buffer mode selection.  The underlying write path already batches
 * through stream->buf; the modes are interpreted as follows:
 *   _IOFBF/_IOLBF: full buffering with the requested (or default) size.
 *   _IONBF: one-byte buffer so every write goes out immediately.
 */

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
	if (!stream)
		return -1;

	if (__stdio_flush_wbuf(stream) < 0)
		return -1;
	stream->buf_used = 0;

	switch (mode) {
	case _IOFBF:
	case _IOLBF:
		if (buf && size > 0) {
			if (stream->buf && !(stream->flags & __IO_SYNC))
				free(stream->buf);
			stream->buf = buf;
			stream->buf_size = size;
			stream->flags |= __IO_SYNC; /* caller-owned buffer */
		} else if (size > 0 && size != stream->buf_size) {
			char *nb = malloc(size);

			if (!nb)
				return -1;
			if (stream->buf && !(stream->flags & __IO_SYNC))
				free(stream->buf);
			stream->buf = nb;
			stream->buf_size = size;
			stream->flags &= (unsigned char)~__IO_SYNC;
		}
		break;
	case _IONBF:
		stream->buf_size = 1;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	stream->read_ptr = NULL;
	stream->read_end = NULL;
	return 0;
}
