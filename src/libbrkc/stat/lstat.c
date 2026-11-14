#include <fcntl.h>
#include <syscall.h>
#include <sys/stat.h>

int lstat(const char *path, struct stat *buf)
{
	return fstatat(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}
