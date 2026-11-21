#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dirent_impl.h"

/*
 * Open a directory stream.  O_DIRECTORY is the fast rejection, and a
 * follow-up fstat() closes the gap for descriptors the kernel did
 * not check (opening a plain file with O_DIRECTORY succeeds there).
 */
DIR *opendir(const char *path)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY);
	struct stat st;
	DIR *d;

	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) < 0) {
		int save = errno;

		close(fd);
		errno = save;
		return NULL;
	}
	if (!S_ISDIR(st.st_mode)) {
		close(fd);
		errno = ENOTDIR;
		return NULL;
	}
	d = malloc(sizeof(*d));
	if (!d) {
		int save = errno;

		close(fd);
		errno = save;
		return NULL;
	}
	d->fd = fd;
	d->next = d->buf;
	d->end = d->buf;
	d->tell = 0;
	return d;
}
