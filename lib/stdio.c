#include <brk/fcntl.h>
#include <brk/macros.h>
#include <brk/printf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct string_display {
	char *buf;
	size_t size;
	size_t pos;
};

static int snprintf_display_write(struct display *dis, char const *buf,
				  size_t len, size_t *wlen)
{
	struct string_display *sd = dis->priv;
	size_t n = min(len, sd->size - sd->pos);
	if (n > 0) {
		memcpy(sd->buf + sd->pos, buf, n);
		sd->pos += n;
	}

	if (wlen)
		*wlen = len;

	return 0;
}

int vsnprintf(char *buf, size_t size, char const *format, va_list ap)
{
	struct string_display sd = {
		.buf = buf,
		.size = size,
		.pos = 0,
	};
	struct display dis = {
		.write = snprintf_display_write,
		.priv = &sd,
	};
	return printf_core(&dis, format, ap);
}

int snprintf(char *buf, size_t size, char const *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vsnprintf(buf, size, format, ap);
	va_end(ap);
	return ret;
}

int printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vfprintf(stdout, format, ap);
	va_end(ap);
	return ret;
}

int vprintf(const char *format, va_list ap)
{
	return vfprintf(stdout, format, ap);
}

int fprintf(FILE *stream, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vfprintf(stream, format, ap);
	va_end(ap);
	return ret;
}

static bool file_buf_full(FILE *stream)
{
	return stream->buf_used >= stream->buf_size;
}

static bool file_buf_empty(FILE *stream)
{
	return stream->buf_used == 0;
}

static int file_flush(FILE *stream)
{
	if (file_buf_empty(stream))
		return 0;
	char *p = stream->buf;
	char *end = p + stream->buf_used;
	while (p < end) {
		ssize_t n = write(stream->fd, p, end - p);
		if (n < 0)
			return -1;
		p += n;
	}
	stream->buf_used = 0;
	return 0;
}

static int file_write_sync(FILE *stream, const char *buf, size_t len,
			   size_t *wlen)
{
	while (len > 0) {
		size_t chunk = min(len, stream->buf_size - stream->buf_used);
		memcpy(stream->buf + stream->buf_used, buf, chunk);
		stream->buf_used += chunk;
		len -= chunk;
		buf += chunk;
		if (file_buf_full(stream) && file_flush(stream) < 0)
			return -1;
	}
	if (file_flush(stream) < 0)
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
		if (file_buf_full(stream) && file_flush(stream) < 0)
			return -1;
		int c = *buf++;
		stream->buf[stream->buf_used++] = c;
		if (c == '\n' && file_flush(stream) < 0)
			return -1;
		--left;
	}
	if (wlen)
		*wlen = len;
	return 0;
}

static int fprintf_display_write(struct display *dis, char const *buf,
				 size_t len, size_t *wlen)
{
	FILE *stream = dis->priv;
	if (stream->sync) {
		return file_write_sync(stream, buf, len, wlen);
	} else {
		return file_write(stream, buf, len, wlen);
	}
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
	struct display dis = {
		.write = fprintf_display_write,
		.priv = stream,
	};
	return printf_core(&dis, format, ap);
}

void perror(const char *s)
{
	fprintf(stderr, "%s: %s\n", s, strerror(errno));
}

int putc(int c, FILE *stream)
{
	return fputc(c, stream);
}

int fputc(int c, FILE *stream)
{
	return fprintf(stream, "%c", c);
}

int putchar(int c)
{
	return putc(c, stdout);
}

int fputs(const char *s, FILE *stream)
{
	return fprintf(stream, "%s\n", s);
}

int puts(const char *s)
{
	return fputs(s, stdout);
}

static char stdin_buf[1024];
static FILE stdin_file = {
	.fd = STDIN_FILENO,
	.buf = stdin_buf,
	.buf_size = sizeof(stdin_buf),
	.buf_used = 0,
	.sync = false,
};

static char stdout_buf[1024];
static FILE stdout_file = {
	.fd = STDOUT_FILENO,
	.buf = stdout_buf,
	.buf_size = sizeof(stdout_buf),
	.buf_used = 0,
	.sync = false,
};

static char stderr_buf[1024];
static FILE stderr_file = {
	.fd = STDERR_FILENO,
	.buf = stderr_buf,
	.buf_size = sizeof(stderr_buf),
	.buf_used = 0,
	.sync = false,
};

FILE *__stdin_file = &stdin_file;
FILE *__stdout_file = &stdout_file;
FILE *__stderr_file = &stderr_file;

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

int fclose(FILE *stream)
{
	if (!stream)
		return -1;
	if (stream == stdin || stream == stdout || stream == stderr)
		return -1;
	if (file_flush(stream) < 0)
		return -1;
	close(stream->fd);
	free(stream->buf);
	free(stream);
	return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t wlen = 0;
	if (!stream)
		return 0;
	if (file_write(stream, ptr, size * nmemb, NULL) < 0)
		return -1;
	return wlen;
}

int fseek(FILE *stream, long offset, int whence)
{
	if (!stream)
		return -1;
	return lseek(stream->fd, offset, whence);
}

long ftell(FILE *stream)
{
	if (!stream)
		return -1;
	return lseek(stream->fd, 0, SEEK_CUR);
}

int fflush(FILE *stream)
{
	return file_flush(stream);
}

int ferror(FILE *stream)
{
	return errno;
}

int feof(FILE *stream)
{
	return 0;
}
