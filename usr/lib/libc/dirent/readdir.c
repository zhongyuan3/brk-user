#include <errno.h>
#include <string.h>

#include "dirent_impl.h"

/*
 * Hand out the next entry, converted into the fixed dirent layout.
 * The returned pointer is valid until the next readdir() on the same
 * stream.  On end of directory or error the result is NULL; a real
 * error is visible through errno.
 */
struct dirent *readdir(DIR *d)
{
	const struct posix_dent *dent;
	size_t namelen;
	int rc;

	if (!d) {
		errno = EINVAL;
		return NULL;
	}

	rc = __dir_next(d, &dent);
	if (rc != __DIR_ENTRY) {
		if (rc == __DIR_EOF)
			errno = 0;
		return NULL;
	}

	namelen = strnlen(dent->d_name, sizeof(d->cur.d_name) - 1);
	d->cur.d_ino = (__brk_s32)dent->d_ino;
	d->cur.d_off = (__brk_s32)d->tell;
	d->cur.d_reclen = (unsigned short)(offsetof(struct dirent, d_name) +
					  namelen + 1);
	d->cur.d_type = dent->d_type;
	memcpy(d->cur.d_name, dent->d_name, namelen);
	d->cur.d_name[namelen] = '\0';

	d->next += dent->d_reclen;
	d->tell++;
	return &d->cur;
}
