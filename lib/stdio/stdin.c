#include <stdio.h>
#include <unistd.h>

static char stdin_buf[1024];
static FILE stdin_file = {
	.fd = STDIN_FILENO,
	.buf = stdin_buf,
	.buf_size = sizeof(stdin_buf),
	.buf_used = 0,
	.sync = false,
};

FILE *__stdin_file = &stdin_file;
