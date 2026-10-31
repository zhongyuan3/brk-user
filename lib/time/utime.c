#include <syscall.h>
#include <time.h>

int utime(const char *path, const struct utimbuf *times)
{
	return syscall(SYS_utime, path, times);
}
