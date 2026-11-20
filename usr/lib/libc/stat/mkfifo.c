#include <fcntl.h>
#include <sys/stat.h>

int mkfifo(const char *path, mode_t mode)
{
	return mknodat(AT_FDCWD, path, S_IFIFO | (mode & 07777), 0);
}
