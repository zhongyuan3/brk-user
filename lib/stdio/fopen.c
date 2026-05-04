#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int mode_to_flags(const char *mode)
{
	int flags = O_RDONLY;
	bool rw = false;
	while (*mode) {
		switch (*mode++) {
		case 'r':
			break;
		case 'w':
			flags |= O_TRUNC;
			if (!rw)
				flags |= O_WRONLY;
			break;
		case 'a':
			flags |= O_APPEND;
			flags |= O_CREAT;
			break;
		case '+':
			flags &= ~O_RDONLY;
			flags &= ~O_WRONLY;
			flags |= O_RDWR;
			break;
		}
	}
	return flags;
}

FILE *fopen(const char *path, const char *mode)
{
	int flags = mode_to_flags(mode);
	int fd = open(path, flags, 0666);
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
	stream->sync = false;
	return stream;
}
