#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	return syscall(SYS_FCHMODAT, dirfd, path, mode, flags);
}
