#include <syscall.h>
#include <sys/stat.h>

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
	return syscall(SYS_mknodat, dirfd, path, mode, dev);
}
