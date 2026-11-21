#include <stdio.h>
#include <unistd.h>

#include "dirent_impl.h"

/*
 * Reset the kernel cursor and the conversion state.  The tell
 * counter restarts, so telldir() cookies taken before the rewind
 * still address the same entries.
 */
void rewinddir(DIR *d)
{
	if (!d)
		return;
	lseek(d->fd, 0, SEEK_SET);
	d->next = d->buf;
	d->end = d->buf;
	d->tell = 0;
}