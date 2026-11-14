#ifndef LIBBRKC_STATX_IMPL_H
#define LIBBRKC_STATX_IMPL_H

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

#define STATX_NS_PER_SEC 1000000000LL

static inline void statx_to_stat(const struct statx *stx, struct stat *st)
{
	memset(st, 0, sizeof(*st));

	st->st_dev = stx->stx_dev;
	st->st_ino = stx->stx_ino;
	st->st_mode = stx->stx_mode;
	st->st_nlink = stx->stx_nlink;
	st->st_uid = stx->stx_uid;
	st->st_gid = stx->stx_gid;
	st->st_rdev = stx->stx_rdev;
	st->st_size = stx->stx_size;
	st->st_blksize = stx->stx_blksize;
	st->st_blocks = stx->stx_blocks;

	st->st_atime = stx->stx_atime_ns / STATX_NS_PER_SEC;
	st->st_atime_nsec = (__u64)(stx->stx_atime_ns % STATX_NS_PER_SEC);
	st->st_mtime = stx->stx_mtime_ns / STATX_NS_PER_SEC;
	st->st_mtime_nsec = (__u64)(stx->stx_mtime_ns % STATX_NS_PER_SEC);
	st->st_ctime = stx->stx_ctime_ns / STATX_NS_PER_SEC;
	st->st_ctime_nsec = (__u64)(stx->stx_ctime_ns % STATX_NS_PER_SEC);
}

static inline int statx_at(int dirfd, const char *path, struct stat *st,
			   int flags)
{
	struct statx stx;
	long ret = syscall(SYS_statx, dirfd, path, flags, &stx);

	if (ret < 0)
		return -1;

	statx_to_stat(&stx, st);
	return 0;
}

#endif
