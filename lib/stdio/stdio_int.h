#ifndef _STDIO_INT_H
#define _STDIO_INT_H

#include <stdio.h>

#include "stdio_impl.h"

int __stdio_file_write(FILE *stream, char const *buf, size_t len, size_t *wlen);
int __stdio_flush_wbuf(FILE *stream);
int __stdio_purge_readbuf(FILE *stream);

#endif
