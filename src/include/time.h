#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/time.h>
#include <sys/types.h>
#include <uapi/time.h>

#define CLOCKS_PER_SEC 1000

time_t time(time_t *tloc);
clock_t clock(void);
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

struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
time_t mktime(struct tm *tm);
double difftime(time_t t1, time_t t0);
size_t strftime(char *s, size_t max, const char *format,
		const struct tm *tm);

#endif
