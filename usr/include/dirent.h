#ifndef _DIRENT_H
#define _DIRENT_H

#include <stddef.h>
#include <sys/types.h>
#include <uapi/dirent.h>

/*
 * POSIX.1-2024 directory entry returned by posix_getdents().  d_reclen
 * includes any trailing padding required to align the next entry.
 */
typedef __brk_ulong_t reclen_t;

struct posix_dent {
	__brk_ino_t d_ino;
	reclen_t d_reclen;
	unsigned char d_type;
	char d_name[];
};

/* Layout used by readdir(). */
struct dirent {
	__brk_ino_t d_ino;
	__brk_off_t d_off;
	__brk_u16 d_reclen;
	__brk_u8 d_type;
	char d_name[256];
};

ssize_t posix_getdents(int fd, void *dirp, size_t count, int flags);

/*
 * Directory streams.  telldir() cookies are stable entry indexes:
 * they stay valid across rewinddir() and seekdir() rebuilds any
 * position by restarting the kernel cursor and skipping forward.
 */
typedef struct __dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);
long telldir(DIR *dir);
void seekdir(DIR *dir, long loc);

#endif
