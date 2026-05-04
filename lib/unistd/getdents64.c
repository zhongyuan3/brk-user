#include <syscall.h>
#include <unistd.h>

ssize_t getdents64(int fd, void *dirp, size_t count)
{
	return syscall(SYS_getdents64, fd, dirp, count);
}
