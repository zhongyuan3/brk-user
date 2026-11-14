#include <stdarg.h>
#include <sys/types.h>
#include <syscall.h>
#include <fcntl.h>

int open(const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	return openat(AT_FDCWD, path, flags, mode);
}
