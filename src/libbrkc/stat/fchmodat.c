#include <sys/stat.h>
#include <syscall.h>

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	(void)flags;
	return syscall(SYS_fchmodat, dirfd, path, mode);
}
