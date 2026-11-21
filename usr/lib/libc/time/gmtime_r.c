#include <time.h>

#include "time_impl.h"

/*
 * Reentrant calendar conversions writing into caller storage.
 */
struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	if (!timep || !result)
		return NULL;
	return __brk_gmtime(*timep, result);
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	time_t local;

	if (!timep || !result)
		return NULL;
	local = *timep + brk_tz_offset();
	return __brk_gmtime(local, result);
}
