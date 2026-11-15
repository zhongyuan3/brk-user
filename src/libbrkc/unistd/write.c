#include <syscall.h>
#include <unistd.h>

ssize_t write(int fd, const void *buf, size_t count)
{
	return syscall(SYS_WRITE, fd, buf, count);
}
