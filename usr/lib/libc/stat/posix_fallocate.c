#include <errno.h>
#include <fcntl.h>

int posix_fallocate(int fd, off_t offset, off_t len)
{
	if (offset < 0 || len <= 0)
		return EINVAL;

	if (fallocate(fd, 0, offset, len) == 0)
		return 0;
	return errno;
}
