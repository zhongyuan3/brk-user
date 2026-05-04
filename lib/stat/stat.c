#include <syscall.h>
#include <sys/stat.h>

int stat(const char *path, struct stat *buf)
{
	return syscall(SYS_stat, path, buf);
}
