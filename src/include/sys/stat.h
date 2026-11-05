#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>
#include <uapi/stat.h>

int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, dev_t dev);
int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev);

int fstat(int fd, struct stat *buf);
int stat(const char *path, struct stat *buf);
int lstat(const char *path, struct stat *buf);

int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
mode_t umask(mode_t mask);

#endif
