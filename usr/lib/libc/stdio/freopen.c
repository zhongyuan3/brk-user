#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "stdio_impl.h"

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
	int fd;

	if (!stream)
		return NULL;

	/* Reopen semantics: flush pending output, close the old descriptor
	 * and attach a freshly opened one, keeping the FILE identity. */
	(void)fflush(stream);
	if (stream->fd >= 0)
		close(stream->fd);

	/* The mode string was already validated for fopen; reuse it. */
	{
		int acc = -1;
		int plus = 0;
		const char *m = mode;

		for (; m && *m; ++m) {
			switch (*m) {
			case 'r':
				acc = 0;
				break;
			case 'w':
				acc = 1;
				break;
			case 'a':
				acc = 2;
				break;
			case '+':
				plus = 1;
				break;
			default:
				break;
			}
		}
		if (acc < 0) {
			errno = EINVAL;
			goto fail;
		}
		if (acc == 0)
			fd = plus ? O_RDWR : O_RDONLY;
		else if (acc == 1)
			fd = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
		else
			fd = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	}

	fd = open(path, fd, 0666);
	if (fd < 0)
		goto fail;

	stream->fd = fd;
	stream->buf_used = 0;
	stream->read_ptr = NULL;
	stream->read_end = NULL;
	stream->flags = 0;
	stream->unget_valid = 0;
	return stream;

fail:
	stream->fd = -1;
	stream->flags |= __IO_ERR;
	return NULL;
}
