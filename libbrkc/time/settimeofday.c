#include <errno.h>
#include <sys/time.h>
#include <time.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	struct timespec ts;

	(void)tz;
	if (!tv)
		return 0;
	if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
		errno = EINVAL;
		return -1;
	}

	ts.tv_sec = tv->tv_sec;
	ts.tv_nsec = tv->tv_usec * 1000L;
	return clock_settime(CLOCK_REALTIME, &ts);
}
