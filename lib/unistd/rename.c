#include <syscall.h>
#include <unistd.h>

int rename(const char *oldpath, const char *newpath)
{
	return syscall(SYS_rename, oldpath, newpath);
}
