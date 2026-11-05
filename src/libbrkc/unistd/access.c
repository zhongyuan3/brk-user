#include <syscall.h>
#include <unistd.h>

int access(const char *path, int mode)
{
	return syscall(SYS_access, path, mode);
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	return syscall(SYS_faccessat2, dirfd, path, mode, flags);
}
