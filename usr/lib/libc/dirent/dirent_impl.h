#ifndef _DIRENT_IMPL_H
#define _DIRENT_IMPL_H

#include <dirent.h>
#include <stddef.h>

#define DIR_BUF_SIZE 4096

/*
 * DIR layout — include only from libc dirent sources.  buf holds raw
 * posix_dent records from the last posix_getdents() refill; next..end
 * is the unconverted remainder.  tell is the index of the next entry
 * to hand out, so telldir() cookies are stable entry indexes and
 * seekdir() can rebuild any position by seeking the kernel cursor to
 * zero and skipping forward.
 */
struct __dir {
	int fd;
	char *next;		/* next raw entry */
	char *end;		/* end of raw entries */
	long tell;		/* index of the next entry to hand out */
	struct dirent cur;	/* entry returned by the last readdir() */
	char buf[DIR_BUF_SIZE];
};

#define __DIR_EOF 0
#define __DIR_ERR (-1)
#define __DIR_ENTRY 1

/*
 * dir_next.c: advance d->next to the next raw entry, refilling from
 * the kernel when exhausted.  Returns __DIR_ENTRY with *dent set,
 * __DIR_EOF at end of directory, or __DIR_ERR with errno set.
 */
int __dir_next(DIR *d, const struct posix_dent **dent);

#endif
