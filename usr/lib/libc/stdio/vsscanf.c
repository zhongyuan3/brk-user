#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "scan_impl.h"

int vsscanf(const char *str, const char *format, va_list ap)
{
	struct scan_src src;

	if (!str)
		return EOF;
	src.fp = NULL;
	src.str = str;
	src.pending_len = 0;
	src.consumed = 0;
	return __scan_core(&src, format, ap);
}
