#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>
#include <uapi/fcntl.h>

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, mode_t mode);
int creat(const char *path, mode_t mode);
int fallocate(int fd, int mode, long offset, long len);

#endif
