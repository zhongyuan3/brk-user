#include <stdio.h>
#include <unistd.h>

static char stderr_buf[1024];
static FILE stderr_file = {
	.fd = STDERR_FILENO,
	.buf = stderr_buf,
	.buf_size = sizeof(stderr_buf),
	.buf_used = 0,
	.sync = true,
};

FILE *__stderr_file = &stderr_file;
