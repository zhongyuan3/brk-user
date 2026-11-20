#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* ISO C remove(): deletes a file or an empty directory. */
int remove(const char *path)
{
	if (unlink(path) == 0)
		return 0;
	if (errno == EISDIR || errno == EPERM || errno == EACCES) {
		int saved = errno;

		if (rmdir(path) == 0)
			return 0;
		errno = saved;
	}
	return -1;
}
