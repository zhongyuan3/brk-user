#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>
#include <uapi/fcntl.h>

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, mode_t mode);
int creat(const char *path, mode_t mode);
int posix_fallocate(int fd, off_t offset, off_t len);

#endif
