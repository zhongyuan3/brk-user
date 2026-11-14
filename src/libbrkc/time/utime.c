#include <fcntl.h>
#include <stddef.h>
#include <syscall.h>
#include <time.h>

int utime(const char *path, const struct utimbuf *times)
{
	struct timespec ts[2];

	if (!times)
		return utimensat(AT_FDCWD, path, NULL, 0);

	ts[0].tv_sec = times->actime;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = times->modtime;
	ts[1].tv_nsec = 0;
	return utimensat(AT_FDCWD, path, ts, 0);
}
