#include <syscall.h>
#include <time.h>

int clock_gettime(clockid_t clockid, struct timespec *tp)
{
	return syscall(SYS_clock_gettime, clockid, tp);
}
