#include <syscall.h>
#include <time.h>
#include <uapi/syscall_internal.h>

int utime(const char *path, const struct utimbuf *times)
{
	return syscall(SYS_utime, path, times);
}
