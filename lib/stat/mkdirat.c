#include <syscall.h>
#include <sys/stat.h>

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	return syscall(SYS_mkdirat, dirfd, path, mode);
}
