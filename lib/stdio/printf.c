#include <stdio.h>

int printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vfprintf(stdout, format, ap);
	va_end(ap);
	return ret;
}
