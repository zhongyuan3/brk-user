#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int unlink(const char *path)
{
	return unlinkat(AT_FDCWD, path, 0);
}
