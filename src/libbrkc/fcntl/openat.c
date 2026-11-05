#include <syscall.h>
#include <fcntl.h>

int openat(int dirfd, const char *path, int flags, mode_t mode)
{
	return syscall(SYS_openat, dirfd, path, flags, mode);
}
