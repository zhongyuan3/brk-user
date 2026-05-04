#include <stdio.h>
#include <unistd.h>

#include "stdio_impl.h"

static char stdin_buf[1024];
static FILE stdin_file = {
	.fd = STDIN_FILENO,
	.buf = stdin_buf,
	.buf_size = sizeof(stdin_buf),
	.buf_used = 0,
	.read_ptr = NULL,
	.read_end = NULL,
	.flags = 0,
};

FILE *__stdin_file = &stdin_file;
