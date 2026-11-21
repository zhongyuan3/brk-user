#include <syscall.h>
#include <time.h>

int utimensat(int dirfd, const char *path, const struct timespec times[2],
	      int flags)
{
	return syscall(SYS_SET_FILE_TIMES, dirfd, path, times, flags);
}
