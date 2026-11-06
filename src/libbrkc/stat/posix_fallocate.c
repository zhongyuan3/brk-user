#include <errno.h>
#include <fcntl.h>
#include <syscall.h>

int posix_fallocate(int fd, off_t offset, off_t len)
{
	long ret = syscall(SYS_posix_fallocate, fd, offset, len);

	if (ret == -1)
		return errno;
	return 0;
}
