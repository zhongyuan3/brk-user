#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int test_fail_msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	return -1;
}

int test_skip(const char *fmt, ...)
{
	va_list ap;

	fputs("[SKIP] ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	return TEST_SKIP_CODE;
}

int test_tmpdir(char *buf, size_t size, const char *tag)
{
	int n = snprintf(buf, size, "/tmp/brktest_%s_%ld", tag, (long)getpid());

	if (n < 0 || (size_t)n >= size)
		return -1;
	(void)rmdir(buf);
	if (mkdir(buf, 0755) != 0)
		return -1;
	return 0;
}
