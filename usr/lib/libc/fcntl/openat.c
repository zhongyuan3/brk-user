#include <syscall.h>
#include <fcntl.h>

int openat(int dirfd, const char *path, int flags, mode_t mode)
{
	return syscall(SYS_OPEN, dirfd, path, flags, mode);
}
