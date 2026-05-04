#include <syscall.h>
#include <fcntl.h>

int open(const char *path, int flags, ...)
{
	return syscall(SYS_open, path, flags, 0);
}
