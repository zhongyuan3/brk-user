#include <syscall.h>
#include <time.h>

int clock_settime(clockid_t clockid, const struct timespec *tp)
{
	return syscall(SYS_CLOCK_SETTIME, clockid, tp);
}
