#include <syscall.h>
#include <sys/stat.h>

int lstat(const char *path, struct stat *buf)
{
	return syscall(SYS_lstat, path, buf);
}
