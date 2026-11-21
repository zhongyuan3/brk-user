#include <stdarg.h>
#include <stdio.h>

int sscanf(const char *str, const char *format, ...)
{
	va_list ap;
	int rc;

	va_start(ap, format);
	rc = vsscanf(str, format, ap);
	va_end(ap);
	return rc;
}
