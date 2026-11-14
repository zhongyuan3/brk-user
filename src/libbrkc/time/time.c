#include <syscall.h>
#include <time.h>

time_t time(time_t *tloc)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return (time_t)-1;

	if (tloc)
		*tloc = ts.tv_sec;
	return ts.tv_sec;
}
