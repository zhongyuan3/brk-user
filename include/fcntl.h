#ifndef _FCNTL_H
#define _FCNTL_H

#include <brk/fcntl.h>
#include <sys/types.h>

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, mode_t mode);

#endif
