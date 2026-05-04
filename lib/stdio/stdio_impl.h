#ifndef BRK_STDIO_IMPL_H
#define BRK_STDIO_IMPL_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * Full FILE layout — include only from libc stdio sources.
 * Public <stdio.h> keeps FILE as an incomplete type.
 */
struct __io_file {
	int (*write)(FILE *stream, char const *buf, size_t len, size_t *wlen);
	char *buf;
	size_t buf_used;
	size_t buf_size;
	char *read_ptr;
	char *read_end;
	int fd;
	unsigned char flags;
};

#define __IO_EOF 1u
#define __IO_ERR 2u
#define __IO_SYNC 4u

#endif
