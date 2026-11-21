#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "scan_impl.h"

int vfscanf(FILE *stream, const char *format, va_list ap)
{
	struct scan_src src;

	if (!stream)
		return EOF;
	src.fp = stream;
	src.str = NULL;
	src.pending_len = 0;
	src.consumed = 0;
	return __scan_core(&src, format, ap);
}
