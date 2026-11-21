#include <stdarg.h>
#include <stdio.h>

int scanf(const char *format, ...)
{
	va_list ap;
	int rc;

	va_start(ap, format);
	rc = vfscanf(stdin, format, ap);
	va_end(ap);
	return rc;
}
