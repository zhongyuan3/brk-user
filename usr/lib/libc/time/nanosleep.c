#include <syscall.h>
#include <time.h>

int nanosleep(const struct timespec *duration, struct timespec *rem)
{
	return clock_nanosleep(CLOCK_MONOTONIC, 0, duration, rem);
}
