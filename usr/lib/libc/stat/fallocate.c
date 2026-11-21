#include <fcntl.h>
#include <syscall.h>

int fallocate(int fd, int mode, off_t offset, off_t len)
{
	return (int)syscall(SYS_PREALLOCATE, fd, mode, offset, len);
}
