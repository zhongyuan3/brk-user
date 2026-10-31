#include <syscall.h>
#include <sys/statfs.h>

int statfs(const char *path, struct statfs *buf)
{
	return syscall(SYS_statfs, path, buf);
}

int fstatfs(int fd, struct statfs *buf)
{
	return syscall(SYS_fstatfs, fd, buf);
}
