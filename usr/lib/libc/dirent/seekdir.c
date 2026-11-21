#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "dirent_impl.h"

/*
 * Position the stream so the next readdir() hands out entry loc.
 * Forward seeks consume entries directly; a backward or cross-buffer
 * seek rewinds the kernel cursor to zero and skips forward, which
 * keeps cookies valid across rewinddir() too.
 */
void seekdir(DIR *d, long loc)
{
	const struct posix_dent *dent;
	long skip;
	int rc;

	if (!d || loc < 0)
		return;

	if (loc == d->tell)
		return;

	if (loc > d->tell) {
		skip = loc - d->tell;
	} else {
		lseek(d->fd, 0, SEEK_SET);
		d->next = d->buf;
		d->end = d->buf;
		d->tell = 0;
		skip = loc;
	}

	while (skip-- > 0) {
		rc = __dir_next(d, &dent);
		if (rc != __DIR_ENTRY) {
			/* Ran past end of directory: park at the end. */
			if (rc == __DIR_EOF)
				errno = 0;
			return;
		}
		d->next += dent->d_reclen;
		d->tell++;
	}
}
