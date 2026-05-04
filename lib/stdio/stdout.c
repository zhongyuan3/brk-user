#include <stdio.h>
#include <unistd.h>

static char stdout_buf[1024];
static FILE stdout_file = {
	.fd = STDOUT_FILENO,
	.buf = stdout_buf,
	.buf_size = sizeof(stdout_buf),
	.buf_used = 0,
	.sync = false,
};

FILE *__stdout_file = &stdout_file;
