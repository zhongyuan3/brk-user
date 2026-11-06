#ifndef _DIRENT_H
#define _DIRENT_H

#include <stddef.h>
#include <sys/types.h>
#include <uapi/dirent.h>

ssize_t posix_getdents(int fd, void *dirp, size_t count, int flags);

#endif
