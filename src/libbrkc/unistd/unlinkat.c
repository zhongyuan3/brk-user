#include <syscall.h>
#include <unistd.h>

int unlinkat(int dirfd, const char *path, int flags)
{
	return syscall(SYS_unlinkat, dirfd, path, flags);
}
