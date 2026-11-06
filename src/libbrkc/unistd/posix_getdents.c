#include <dirent.h>
#include <syscall.h>

ssize_t posix_getdents(int fd, void *dirp, size_t count, int flags)
{
	return syscall(SYS_posix_getdents, fd, dirp, count, flags);
}
