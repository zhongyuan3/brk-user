#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/types.h>
#include <uapi/stat.h>

#include <time.h>

/*
 * POSIX file status.  libc owns this layout; the kernel ABI is
 * struct __brk_stat (uapi/stat.h) and stat/statx_impl.h converts
 * field by field.  Kept in the traditional 128-byte shape.
 */
struct stat {
	dev_t st_dev;			/* 0 */
	ino_t st_ino;			/* 8 */
	mode_t st_mode;			/* 16 */
	nlink_t st_nlink;		/* 20 */
	uid_t st_uid;			/* 24 */
	gid_t st_gid;			/* 28 */
	dev_t st_rdev;			/* 32 */
	unsigned long __pad1;		/* 40 */
	off_t st_size;			/* 48 */
	int st_blksize;			/* 56 */
	int __pad2;			/* 60 */
	long st_blocks;		/* 64 */
	time_t st_atime;		/* 72 */
	unsigned long st_atime_nsec;	/* 80 */
	time_t st_mtime;		/* 88 */
	unsigned long st_mtime_nsec;	/* 96 */
	time_t st_ctime;		/* 104 */
	unsigned long st_ctime_nsec;	/* 112 */
	unsigned int __unused4;		/* 120 */
	unsigned int __unused5;		/* 124 */
};

int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, dev_t dev);
int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev);

int fstat(int fd, struct stat *buf);
int stat(const char *path, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int fstatat(int dirfd, const char *path, struct stat *buf, int flags);
int futimens(int fd, const struct timespec times[2]);
int mkfifo(const char *path, mode_t mode);

int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
mode_t umask(mode_t mask);

#endif
