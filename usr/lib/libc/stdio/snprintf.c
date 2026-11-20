#include <stdio.h>

int snprintf(char *buf, size_t size, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vsnprintf(buf, size, format, ap);
	va_end(ap);
	return ret;
}
