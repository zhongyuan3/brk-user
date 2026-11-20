#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int link(const char *oldpath, const char *newpath)
{
	return linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}
