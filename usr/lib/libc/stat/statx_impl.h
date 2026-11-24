#ifndef LIBBRKC_STATX_IMPL_H
#define LIBBRKC_STATX_IMPL_H

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

#define STATX_NS_PER_SEC 1000000000LL

static inline void statx_to_stat(const struct __brk_stat *stx, struct stat *st)
{
	memset(st, 0, sizeof(*st));

	st->st_dev = stx->st_dev;
	st->st_ino = stx->st_ino;
	st->st_mode = stx->st_mode;
	st->st_nlink = stx->st_nlink;
	st->st_uid = stx->st_uid;
	st->st_gid = stx->st_gid;
	st->st_rdev = stx->st_rdev;
	st->st_size = stx->st_size;
	st->st_blksize = stx->st_blksize;
	st->st_blocks = stx->st_blocks;

	st->st_atime = stx->st_atime_ns / STATX_NS_PER_SEC;
	st->st_atime_nsec = (__brk_u64)(stx->st_atime_ns % STATX_NS_PER_SEC);
	st->st_mtime = stx->st_mtime_ns / STATX_NS_PER_SEC;
	st->st_mtime_nsec = (__brk_u64)(stx->st_mtime_ns % STATX_NS_PER_SEC);
	st->st_ctime = stx->st_ctime_ns / STATX_NS_PER_SEC;
	st->st_ctime_nsec = (__brk_u64)(stx->st_ctime_ns % STATX_NS_PER_SEC);
}

static inline int statx_at(int dirfd, const char *path, struct stat *st,
			   int flags)
{
	struct __brk_stat stx;
	long ret = syscall(SYS_STAT, dirfd, path, flags, &stx);

	if (ret < 0)
		return -1;

	statx_to_stat(&stx, st);
	return 0;
}

#endif
