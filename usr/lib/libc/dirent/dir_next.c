#include <errno.h>

#include "dirent_impl.h"

int __dir_next(DIR *d, const struct posix_dent **dent)
{
	while (d->next >= d->end) {
		ssize_t n = posix_getdents(d->fd, d->buf, sizeof(d->buf), 0);

		if (n < 0)
			return __DIR_ERR;
		if (n == 0)
			return __DIR_EOF;
		d->next = d->buf;
		d->end = d->buf + n;
	}
	*dent = (const struct posix_dent *)d->next;
	return __DIR_ENTRY;
}
