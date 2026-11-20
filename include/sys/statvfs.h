#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <sys/types.h>
#include <uapi/statvfs.h>

int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);

#endif
