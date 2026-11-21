#ifndef _TIME_IMPL_H
#define _TIME_IMPL_H

#include <time.h>

/*
 * Shared broken-down-time conversion: fills *tm from the UTC instant
 * t without timezone adjustment.  Declared in time/gmtime.c, used by
 * the reentrant variants as well.
 */
struct tm *__brk_gmtime(time_t t, struct tm *tm);

#endif
