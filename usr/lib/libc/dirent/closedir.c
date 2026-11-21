#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "dirent_impl.h"

int closedir(DIR *d)
{
	int rc;

	if (!d) {
		errno = EINVAL;
		return -1;
	}
	rc = close(d->fd);
	free(d);
	return rc;
}
