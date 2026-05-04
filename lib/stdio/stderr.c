#include <stdio.h>
#include <unistd.h>

#include "stdio_impl.h"

static char stderr_buf[1024];
static FILE stderr_file = {
	.fd = STDERR_FILENO,
	.buf = stderr_buf,
	.buf_size = sizeof(stderr_buf),
	.buf_used = 0,
	.read_ptr = NULL,
	.read_end = NULL,
	.flags = __IO_SYNC,
};

FILE *__stderr_file = &stderr_file;
