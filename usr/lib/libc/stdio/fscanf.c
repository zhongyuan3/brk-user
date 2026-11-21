#include <stdarg.h>
#include <stdio.h>

int fscanf(FILE *stream, const char *format, ...)
{
	va_list ap;
	int rc;

	va_start(ap, format);
	rc = vfscanf(stream, format, ap);
	va_end(ap);
	return rc;
}
