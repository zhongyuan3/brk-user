#ifndef _TIME_H
#define _TIME_H

#include <sys/time.h>

time_t time(time_t *tloc);
int clock_gettime(clockid_t clockid, struct timespec *tp);
int clock_getres(clockid_t clockid, struct timespec *res);
int clock_settime(clockid_t clockid, const struct timespec *tp);
int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *req,
		    struct timespec *rem);

int utime(const char *path, const struct utimbuf *times);
int utimes(const char *path, const struct timeval times[2]);
int utimensat(int dirfd, const char *path, const struct timespec times[2],
	      int flags);

int nanosleep(const struct timespec *req, struct timespec *rem);

long brk_tz_offset(void);

#endif
