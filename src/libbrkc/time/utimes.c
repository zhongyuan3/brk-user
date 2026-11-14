#include <fcntl.h>
#include <stddef.h>
#include <syscall.h>
#include <time.h>

int utimes(const char *path, const struct timeval times[2])
{
	struct timespec ts[2];

	if (!times)
		return utimensat(AT_FDCWD, path, NULL, 0);

	ts[0].tv_sec = times[0].tv_sec;
	ts[0].tv_nsec = times[0].tv_usec * 1000L;
	ts[1].tv_sec = times[1].tv_sec;
	ts[1].tv_nsec = times[1].tv_usec * 1000L;
	return utimensat(AT_FDCWD, path, ts, 0);
}
