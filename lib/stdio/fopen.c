#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stdio_impl.h"

static int mode_to_flags(const char *mode)
{
	int acc = -1;
	bool plus = false;

	if (!mode || !*mode) {
		errno = EINVAL;
		return -1;
	}

	for (; *mode; ++mode) {
		switch (*mode) {
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
			plus = true;
			break;
		case 'b':
			break;
		default:
			errno = EINVAL;
			return -1;
		}
	}

	if (acc < 0) {
		errno = EINVAL;
		return -1;
	}

	if (acc == 0)
		return plus ? O_RDWR : O_RDONLY;
	if (acc == 1)
		return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
}

FILE *fopen(const char *path, const char *mode)
{
	int flags = mode_to_flags(mode);
	int fd;

	if (flags < 0)
		return NULL;
	fd = open(path, flags, 0666);
	if (fd < 0)
		return NULL;
	FILE *stream = malloc(sizeof(FILE));
	if (!stream) {
		close(fd);
		return NULL;
	}
	stream->fd = fd;
	stream->buf = malloc(1024 * sizeof(char));
	if (!stream->buf) {
		close(fd);
		free(stream);
		return NULL;
	}
	stream->buf_size = 1024;
	stream->buf_used = 0;
	stream->read_ptr = NULL;
	stream->read_end = NULL;
	stream->write = NULL;
	stream->flags = 0;
	return stream;
}
