#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int symlink(const char *target, const char *linkpath)
{
	return symlinkat(target, AT_FDCWD, linkpath);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath)
{
	return syscall(SYS_symlinkat, target, newdirfd, linkpath);
}
