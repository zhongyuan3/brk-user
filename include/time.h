#ifndef _TIME_H
#define _TIME_H

#include <sys/time.h>

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

struct utimbuf {
	time_t actime;
	time_t modtime;
};

int utime(const char *path, const struct utimbuf *times);
int utimes(const char *path, const struct timeval times[2]);
int utimensat(int dirfd, const char *path, const struct timespec times[2],
	      int flags);

struct tm {
	int tm_sec; /* Seconds.	[0-60] (1 leap second) */
	int tm_min; /* Minutes.	[0-59] */
	int tm_hour; /* Hours.	[0-23] */
	int tm_mday; /* Day.		[1-31] */
	int tm_mon; /* Month.	[0-11] */
	int tm_year; /* Year	- 1900.  */
	int tm_wday; /* Day of week.	[0-6] */
	int tm_yday; /* Days in year.[0-365]	*/
	int tm_isdst; /* DST.		[-1/0/1]*/
};

int nanosleep(const struct timespec *req, struct timespec *rem);

#endif
