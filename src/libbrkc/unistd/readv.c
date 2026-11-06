#include <sys/uio.h>
#include <syscall.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	return syscall(SYS_readv, fd, iov, iovcnt);
}
