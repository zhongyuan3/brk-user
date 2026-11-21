#include <sys/uio.h>
#include <syscall.h>

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	return syscall(SYS_WRITE_IOV, fd, iov, iovcnt);
}
