#ifndef _DIRENT_H
#define _DIRENT_H

#include <stddef.h>
#include <sys/types.h>
#include <uapi/dirent.h>

/*
 * POSIX.1-2024 directory entry returned by posix_getdents().  d_reclen
 * includes any trailing padding required to align the next entry.
 */
typedef __kernel_ulong_t reclen_t;

struct posix_dent {
	__kernel_ino_t d_ino;
	reclen_t d_reclen;
	unsigned char d_type;
	char d_name[];
};

/* Layout used by readdir(). */
struct dirent {
	__s32 d_ino;
	__s32 d_off;
	__u16 d_reclen;
	__u8 d_type;
	char d_name[256];
};

ssize_t posix_getdents(int fd, void *dirp, size_t count, int flags);

#endif
