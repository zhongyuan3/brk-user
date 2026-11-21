#include <syscall.h>
#include <time.h>

int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *req,
		    struct timespec *rem)
{
	return syscall(SYS_SLEEP_CLOCK, clockid, flags, req, rem);
}
