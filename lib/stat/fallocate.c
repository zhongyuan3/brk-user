#include <syscall.h>

int fallocate(int fd, int mode, long offset, long len)
{
	return syscall(SYS_fallocate, fd, mode, offset, len);
}
