#include <sys/time.h>
#include <time.h>

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct timespec ts;

	(void)tz;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return -1;

	if (tv) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000L;
	}
	return 0;
}
