#include <stdio.h>
#include <unistd.h>

#include "stdio_impl.h"

static char stdout_buf[1024];
static FILE stdout_file = {
	.fd = STDOUT_FILENO,
	.buf = stdout_buf,
	.buf_size = sizeof(stdout_buf),
	.buf_used = 0,
	.read_ptr = NULL,
	.read_end = NULL,
	.flags = 0,
};

FILE *__stdout_file = &stdout_file;
