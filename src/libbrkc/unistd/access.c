#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int access(const char *path, int mode)
{
	return faccessat(AT_FDCWD, path, mode, 0);
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	return syscall(SYS_faccessat, dirfd, path, mode, flags);
}
