#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>

int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
	return syscall(SYS_fstatat, dirfd, path, buf, flags);
}
