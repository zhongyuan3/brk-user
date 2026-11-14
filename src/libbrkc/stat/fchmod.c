#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>

int fchmod(int fd, mode_t mode)
{
	return fchmodat(fd, "", mode, AT_EMPTY_PATH);
}
