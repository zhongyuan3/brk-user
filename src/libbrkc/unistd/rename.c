#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int rename(const char *oldpath, const char *newpath)
{
	return renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd,
	     const char *newpath)
{
	return syscall(SYS_renameat, olddirfd, oldpath, newdirfd, newpath);
}
