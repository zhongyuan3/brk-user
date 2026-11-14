#include <fcntl.h>
#include <syscall.h>
#include <sys/stat.h>

int stat(const char *path, struct stat *buf)
{
	return fstatat(AT_FDCWD, path, buf, 0);
}
