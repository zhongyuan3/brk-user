#ifndef _TIME_H
#define _TIME_H

#include <sys/time.h>

int utime(const char *path, const struct utimbuf *times);
int utimes(const char *path, const struct timeval times[2]);
int utimensat(int dirfd, const char *path, const struct timespec times[2],
	      int flags);

int nanosleep(const struct timespec *req, struct timespec *rem);

#endif
