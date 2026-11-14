#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
	return readlinkat(AT_FDCWD, path, buf, bufsiz);
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
	return syscall(SYS_readlinkat, dirfd, path, buf, bufsiz);
}
