#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <sys/types.h>
#include <uapi/time.h>

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);

#endif
