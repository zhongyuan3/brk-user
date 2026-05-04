#include <syscall.h>
#include <unistd.h>

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
	   int flags)
{
	return syscall(SYS_linkat, olddirfd, oldpath, newdirfd, newpath, flags);
}
