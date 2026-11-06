#include <syscall.h>
#include <time.h>

int clock_getres(clockid_t clockid, struct timespec *res)
{
	return syscall(SYS_clock_getres, clockid, res);
}
